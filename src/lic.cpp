/*
Copyright (c) 2022 Tomas Karabela

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
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

template<typename T>
static inline T clamp(T value, T lower, T upper) {
    if (value < lower) {
        return lower;
    } else if (value > upper) {
        return upper;
    }
    return value;
}

template<typename T>
static inline bool is_one_of(T value, std::initializer_list<T> choices) {
    for (const auto &x: choices) {
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
        return 0.5 + 0.5 * noise.noise(frequency * x, frequency * y);
    }

    inline float sampleImageData(OFX::Image *img, float x, float y) {
        int x_ = int(x);
        int y_ = int(y);
        auto bounds = img->getBounds();

        x_ = clamp(x_, bounds.x1, bounds.x2 - 1);
        y_ = clamp(y_, bounds.y1, bounds.y2 - 1);

        return *(float *) img->getPixelAddress(x_, y_);
    }

public :
    explicit LICProcessor(OFX::ImageEffect &instance)
            : OFX::ImageProcessor(instance), vectorXImg_(nullptr), vectorYImg_(nullptr), frequency(1), num_steps(15) {
    }

    void setVectorXImg(OFX::Image *v) { vectorXImg_ = v; }

    void setVectorYImg(OFX::Image *v) { vectorYImg_ = v; }

    void setFrequency(float d) { frequency = d; }

    void setNumSteps(int d) { num_steps = d; }

    void multiThreadProcessImages(OfxRectI procWindow) override {
        assert(_dstImg->getPixelComponents() == OFX::ePixelComponentRGBA);

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) break;

            auto dstPix = (float *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                auto px0 = (float) x, py0 = (float) y;
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
                    for (int i = 0; i < num_steps; i++) {
                        ux = sampleImageData(vectorXImg_, px, py);
                        uy = sampleImageData(vectorYImg_, px, py);

                        // normalize
                        float umag = sqrtf(ux * ux + uy * uy);
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
                    for (int i = 0; i < num_steps; i++) {
                        ux = sampleImageData(vectorXImg_, px, py);
                        uy = sampleImageData(vectorYImg_, px, py);

                        // normalize
                        float umag = sqrtf(ux * ux + uy * uy);
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

class LICPlugin : public OFX::ImageEffect {
protected :
    OFX::Clip *vectorXClip_;
    OFX::Clip *vectorYClip_;
    OFX::Clip *dstClip_;
    OFX::DoubleParam *frequency_;
    OFX::IntParam *num_steps_;

public :
    explicit LICPlugin(OfxImageEffectHandle handle)
            : ImageEffect(handle), vectorXClip_(nullptr), vectorYClip_(nullptr), dstClip_(nullptr), frequency_(nullptr),
              num_steps_(nullptr) {
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

void LICPlugin::setupAndProcess(LICProcessor &processor, const OFX::RenderArguments &args) {
    std::unique_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> vectorX(vectorXClip_->fetchImage(args.time));
    std::unique_ptr<OFX::Image> vectorY(vectorYClip_->fetchImage(args.time));
    double frequency = frequency_->getValueAtTime(args.time);
    int num_steps = num_steps_->getValueAtTime(args.time);

    if (!dst || !vectorX || !vectorY) {
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
    processor.setFrequency((float) frequency);
    processor.setNumSteps(num_steps);

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    processor.process();
}

void LICPlugin::render(const OFX::RenderArguments &args) {
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

void LICPluginFactory::describe(OFX::ImageEffectDescriptor &desc) {
    fprintf(stderr, "LICPluginFactory::describe...\n");
    desc.setLabels("LIC", "LIC", "Line Integral Convolution");
    desc.setPluginGrouping("LIC");

    desc.addSupportedContext(eContextGeneral);

    // TODO support half float
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(false);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}

void LICPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum contextEnum) {
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

OFX::ImageEffect *LICPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum contextEnum) {
    fprintf(stderr, "LICPluginFactory::createInstance, context = %d...\n", contextEnum);
    return new LICPlugin(handle);
}

namespace OFX {
    namespace Plugin {
        void getPluginIDs(OFX::PluginFactoryArray &ids) {
            static LICPluginFactory p("com.github.tkarabela.licPlugin", 1, 0);
            ids.push_back(&p);
        }
    }
}
