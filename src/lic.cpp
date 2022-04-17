/*
OFX Invert Example plugin, a plugin that illustrates the use of the OFX Support library.

Copyright (C) 2007 The Open Effects Association Ltd
Author Bruno Nicoletti bruno@thefoundry.co.uk

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.
* Neither the name The Open Effects Association Ltd, nor the names of its
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The Open Effects Association Ltd
1 Wardour St
London W1D 6PA
England


*/

#ifdef _WINDOWS
#include <windows.h>
#endif

#include <cstdio>
#include <random>
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"
#include "SimplexNoise.h"
#include "ofxsProcessing.H"

template <typename T>
static inline T clamp(T value, T lower, T upper)
{
    if (value < lower) {
        return lower;
    } else if (value > upper) {
        return upper;
    }
    return value;
}

template <typename T>
static inline bool is_one_of(T value, std::initializer_list<T> choices)
{
    for (const auto &x : choices) {
        if (x == value) return true;
    }
    return false;
}

// Base class for the RGBA and the Alpha processor
class LICProcessor : public OFX::ImageProcessor {
protected :
    OFX::Image *vectorXImg_;
    OFX::Image *vectorYImg_;
    SimplexNoise noise;
    float frequency;
    int num_steps;

    inline float sampleRandomData(float x, float y) {
        return 0.5+0.5*noise.noise(frequency*x, frequency*y);
    }

    inline float sampleImageData(OFX::Image *img, float x, float y) {
        int x_ = int(x);
        int y_ = int(y);
        auto bounds = img->getBounds();

        x_ = clamp(x_, bounds.x1, bounds.x2-1);
        y_ = clamp(y_, bounds.y1, bounds.y2-1);

        return *(float*)img->getPixelAddress(x_, y_);
    }

public :
  /** @brief no arg ctor */
  explicit LICProcessor(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , vectorXImg_(nullptr)
    , vectorYImg_(nullptr)
    , frequency(1)
    , num_steps(15)
  {
  }

    void setVectorXImg(OFX::Image *v) {vectorXImg_ = v;}
    void setVectorYImg(OFX::Image *v) {vectorYImg_ = v;}
    void setFrequency(float d) {frequency = d;}
    void setNumSteps(int d) {num_steps = d;}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow) override
    {
        assert(_dstImg->getPixelComponents() == OFX::ePixelComponentRGBA);

      for(int y = procWindow.y1; y < procWindow.y2; y++) {
            if(_effect.abort()) break;

            auto dstPix = (float *) _dstImg->getPixelAddress(procWindow.x1, y);

            for(int x = procWindow.x1; x < procWindow.x2; x++) {
                auto px0 = (float)x, py0 = (float)y;
                float px, py;
                float acc = 0.0f;
                int samples = 0;

                acc += sampleRandomData(px0, py0);
                samples++;
                float ux = sampleImageData(vectorXImg_, px0, py0);
                float uy = sampleImageData(vectorYImg_, px0, py0);

                if (ux != 0 || uy != 0) {
                    // integrate forward
                    px = px0, py = py0;
                    for (int i = 0; i < num_steps; i++)
                    {
                        ux = sampleImageData(vectorXImg_, px, py);
                        uy = sampleImageData(vectorYImg_, px, py);

                        // normalize
                        float umag = sqrtf(ux*ux + uy*uy);
                        ux /= umag;
                        uy /= umag;

                        px += ux;
                        py += uy;
                        float value = sampleRandomData(px, py);
                        if (std::isnan(value)) break;
                        acc += value;
                        samples++;
                    }

                    // integrate backward
                    px = px0, py = py0;
                    for (int i = 0; i < num_steps; i++)
                    {
                        ux = sampleImageData(vectorXImg_, px, py);
                        uy = sampleImageData(vectorYImg_, px, py);

                        // normalize
                        float umag = sqrtf(ux*ux + uy*uy);
                        ux /= umag;
                        uy /= umag;

                        px -= ux;
                        py -= uy;
                        float value = sampleRandomData(px, py);
                        if (std::isnan(value)) break;
                        acc += value;
                        samples++;
                    }
                } else {
                    // we're starting at a null vector; no point in integrating,
                    // treat this the same as NaN and mask this pixel in output
                }

                float value = acc / samples;
                float alpha = 1.0f;
                if (samples <= 1) {
                    value = 0.0f;
                    alpha = 0.0f;
                }

                dstPix[0] = value; // = R
                dstPix[1] = value; // = G
                dstPix[2] = value; // = B
                dstPix[3] = alpha; // = A

                // increment the dst pixel
                dstPix += 4;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class LICPlugin : public OFX::ImageEffect {
protected :
  // do not need to delete these, the ImageEffect is managing them for us
  OFX::Clip *vectorXClip_;
  OFX::Clip *vectorYClip_;
  OFX::Clip *dstClip_;
    OFX::DoubleParam  *frequency_;
    OFX::IntParam  *num_steps_;

public :
  /** @brief ctor */
  explicit LICPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , vectorXClip_(nullptr)
    , vectorYClip_(nullptr)
    , dstClip_(nullptr)
    , frequency_(nullptr)
    , num_steps_(nullptr)
  {
        fprintf(stderr, "LICPlugin::LICPlugin()...\n");

        vectorXClip_ = fetchClip("VectorX");
        vectorYClip_ = fetchClip("VectorY");
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);

      frequency_ = fetchDoubleParam("frequency");
      num_steps_ = fetchIntParam("num_steps");
  }

  /* Override the render */
  void render(const OFX::RenderArguments &args) override;

  /* set up and run a processor */
  void setupAndProcess(LICProcessor &, const OFX::RenderArguments &args);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
LICPlugin::setupAndProcess(LICProcessor &processor, const OFX::RenderArguments &args)
{
    std::unique_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> vectorX(vectorXClip_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> vectorY(vectorYClip_->fetchImage(args.time));
    double frequency = frequency_->getValueAtTime(args.time);
    int num_steps = num_steps_->getValueAtTime(args.time);

    if (!dst || !vectorX || !vectorY)
    {
        fprintf(stderr, "LICPlugin::setupAndProcess did not get all images, some are NULL\n");
        throw int(1); // XXX need to throw an sensible exception here!
    }

    if (dst->getPixelDepth() != OFX::eBitDepthFloat ||
        vectorX->getPixelDepth() != OFX::eBitDepthFloat ||
        vectorY->getPixelDepth() != OFX::eBitDepthFloat)
    {
        fprintf(stderr, "LICPlugin::setupAndProcess got image with pixel depth != float\n");
        throw int(1); // XXX need to throw an sensible exception here!
    }

    if (!is_one_of(vectorX->getPixelComponents(), {OFX::ePixelComponentAlpha, OFX::ePixelComponentRGB, OFX::ePixelComponentRGBA}) ||
        !is_one_of(vectorY->getPixelComponents(), {OFX::ePixelComponentAlpha, OFX::ePixelComponentRGB, OFX::ePixelComponentRGBA}) ||
        dst->getPixelComponents() != OFX::ePixelComponentRGBA)
    {
        fprintf(stderr, "LICPlugin::setupAndProcess got image with unsupported pixel components\n");
        throw int(1); // XXX need to throw an sensible exception here!
    }

  // set the images
  processor.setDstImg(dst.get());
  processor.setVectorXImg(vectorX.get());
  processor.setVectorYImg(vectorY.get());
    processor.setFrequency((float)frequency);
    processor.setNumSteps(num_steps);

  // set the render window
  processor.setRenderWindow(args.renderWindow);

  // Call the base class process member, this will call the derived templated process code
  processor.process();
}

// the overridden render function
void
LICPlugin::render(const OFX::RenderArguments &args)
{
    if (vectorXClip_->getPixelDepth() != OFX::eBitDepthFloat ||
        vectorYClip_->getPixelDepth() != OFX::eBitDepthFloat ||
        dstClip_->getPixelDepth() != OFX::eBitDepthFloat)
    {
        fprintf(stderr, "LICPlugin::render got clip with pixel depth != float\n");
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    if (!is_one_of(vectorXClip_->getPixelComponents(), {OFX::ePixelComponentAlpha, OFX::ePixelComponentRGB, OFX::ePixelComponentRGBA}) ||
        !is_one_of(vectorYClip_->getPixelComponents(), {OFX::ePixelComponentAlpha, OFX::ePixelComponentRGB, OFX::ePixelComponentRGBA}) ||
        dstClip_->getPixelComponents() != OFX::ePixelComponentRGBA)
    {
        fprintf(stderr, "LICPlugin::render got clip with unsupported pixel components\n");
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    LICProcessor processor(*this);
    setupAndProcess(processor, args);
}

mDeclarePluginFactory(LICPluginFactory, {}, {});

using namespace OFX;
void LICPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    fprintf(stderr, "LICPluginFactory::describe...\n");
    desc.setLabels("LIC", "LIC", "Line Integral Convolution");
  desc.setPluginGrouping("LIC");

  desc.addSupportedContext(eContextGeneral);

  // add supported pixel depths
  // TODO support half float
  desc.addSupportedBitDepth(eBitDepthFloat);

  // set a few flags
  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(false);
  desc.setSupportsTiles(true);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);
}

void LICPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum contextEnum)
{
    fprintf(stderr, "LICPluginFactory::describeInContext, context = %d...\n", contextEnum);
    auto *vectorXClip = desc.defineClip("VectorX");
    vectorXClip->addSupportedComponent(ePixelComponentAlpha);
    vectorXClip->addSupportedComponent(ePixelComponentRGB);
    vectorXClip->addSupportedComponent(ePixelComponentRGBA);
    vectorXClip->setLabels("Vector X", "Vector X", "Vector X");

    auto *vectorYClip = desc.defineClip("VectorY");
    vectorYClip->addSupportedComponent(ePixelComponentAlpha);
    vectorXClip->addSupportedComponent(ePixelComponentRGB);
    vectorXClip->addSupportedComponent(ePixelComponentRGBA);
    vectorYClip->setLabels("Vector Y", "Vector Y", "Vector Y");

    auto *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);

    auto *frequency = desc.defineDoubleParam("frequency");
    frequency->setLabels("frequency", "frequency", "frequency");
    frequency->setScriptName("frequency");
    frequency->setHint("scales the noise size");
    frequency->setDefault(0.2);
    frequency->setRange(0, 2);
    frequency->setIncrement(0.01);
    frequency->setDisplayRange(0, 2);
    frequency->setDoubleType(eDoubleTypeScale);

    auto *num_steps = desc.defineIntParam("num_steps");
    num_steps->setLabels("num_steps", "Num. steps", "Number of steps");
    num_steps->setScriptName("num_steps");
    num_steps->setHint("number of forward/backward integration steps");
    num_steps->setDefault(15);
    num_steps->setRange(1, 50);
    num_steps->setDisplayRange(1, 50);
}

OFX::ImageEffect* LICPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum contextEnum)
{
    fprintf(stderr, "LICPluginFactory::createInstance, context = %d...\n", contextEnum);
    return new LICPlugin(handle);
}

namespace OFX
{
  namespace Plugin
  {
    void getPluginIDs(OFX::PluginFactoryArray &ids)
    {
      static LICPluginFactory p("com.github.tkarabela.licPlugin", 1, 0);
      ids.push_back(&p);
    }
  }
}
