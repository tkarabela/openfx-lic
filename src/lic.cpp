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
#include <array>
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"

#define RANDOM_SIZE 128

// Base class for the RGBA and the Alpha processor
class LICProcessor : public OFX::ImageProcessor {
protected :
    OFX::Image *vectorXImg_;
    OFX::Image *vectorYImg_;
    std::array<float, RANDOM_SIZE*RANDOM_SIZE> randomData;

    float sampleRandomData(float x, float y) const {
        int x_ = (int)x;
        int y_ = (int)y; // TODO bilinear

        x_ %= RANDOM_SIZE;
        y_ %= RANDOM_SIZE;

        return randomData[x_*RANDOM_SIZE + y_];
    }

public :
  /** @brief no arg ctor */
  explicit LICProcessor(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , vectorXImg_(nullptr)
    , vectorYImg_(nullptr)
    , randomData()
  {
      std::random_device rd;
      std::mt19937 e2(rd());
      std::uniform_real_distribution<> dist(0, 1);
      for (auto &x : randomData) {
          x = (float)dist(e2);
      }
  }

    void setVectorXImg(OFX::Image *v) {vectorXImg_ = v;}
    void setVectorYImg(OFX::Image *v) {vectorYImg_ = v;}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow) override
    {
      const int nComponents = 1;
      const float max = 1.0;

      for(int y = procWindow.y1; y < procWindow.y2; y++) {
            if(_effect.abort()) break;

            auto dstPix = (float *) _dstImg->getPixelAddress(procWindow.x1, y);

            for(int x = procWindow.x1; x < procWindow.x2; x++) {

                float value = sampleRandomData((float)x, (float)y);
                *dstPix = value;

                // increment the dst pixel
                dstPix += nComponents;
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

public :
  /** @brief ctor */
  explicit LICPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , vectorXClip_(nullptr)
    , vectorYClip_(nullptr)
    , dstClip_(nullptr)
  {
    vectorXClip_ = fetchClip("VectorX");
    vectorYClip_ = fetchClip("VectorY");
    dstClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
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

    if (dst->getPixelComponents() != OFX::ePixelComponentAlpha ||
        vectorX->getPixelComponents() != OFX::ePixelComponentAlpha ||
        vectorY->getPixelComponents() != OFX::ePixelComponentAlpha)
    {
        fprintf(stderr, "LICPlugin::setupAndProcess got image with pixel components != alpha\n");
        throw int(1); // XXX need to throw an sensible exception here!
    }

  // set the images
  processor.setDstImg(dst.get());
  processor.setVectorXImg(vectorX.get());
  processor.setVectorYImg(vectorY.get());

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

    if (vectorXClip_->getPixelComponents() != OFX::ePixelComponentAlpha ||
        vectorYClip_->getPixelComponents() != OFX::ePixelComponentAlpha ||
        dstClip_->getPixelComponents() != OFX::ePixelComponentAlpha)
    {
        fprintf(stderr, "LICPlugin::render got clip with pixel components != alpha\n");
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    LICProcessor processor(*this);
    setupAndProcess(processor, args);
}

mDeclarePluginFactory(LICPluginFactory, {}, {});

using namespace OFX;
void LICPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
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

void LICPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    ClipDescriptor *vectorXClip = desc.defineClip("VectorX");
    vectorXClip->addSupportedComponent(ePixelComponentAlpha);
    vectorXClip->setLabels("Vector X", "Vector X", "Vector X");

    ClipDescriptor *vectorYClip = desc.defineClip("VectorY");
    vectorYClip->addSupportedComponent(ePixelComponentAlpha);
    vectorYClip->setLabels("Vector Y", "Vector Y", "Vector Y");

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
}

OFX::ImageEffect* LICPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
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
