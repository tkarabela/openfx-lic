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
    bool use_weight_window;
    int weight_window_width;
    int weight_window_offset;
    double _debug_time;

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

    inline float getStepWeight(int signedIdx) const {
        if (!use_weight_window) {
            return 1.0f;
        } else {
            int N = 2*num_steps;
            int idx = (signedIdx + num_steps) % (N + 1);
            int offsetIdx = (weight_window_offset + num_steps) % N;
            int offsetIdx2 = ((weight_window_offset + num_steps) % N) + N;
            int dist = std::min(std::abs(idx - offsetIdx), std::abs(idx - offsetIdx2));

            if (dist > weight_window_width) {
                return 0.0f;
            } else {
                return 1.0f - (float)dist/(float)weight_window_width;
            }
        }
    }

public :
    explicit LICProcessor(OFX::ImageEffect &instance)
            : OFX::ImageProcessor(instance), vectorXImg_(nullptr), vectorYImg_(nullptr), frequency(1), num_steps(15),
              use_weight_window(false), weight_window_width(10), weight_window_offset(0) {
    }

    void setVectorXImg(OFX::Image *v) { vectorXImg_ = v; }

    void setVectorYImg(OFX::Image *v) { vectorYImg_ = v; }

    void setFrequency(float d) { frequency = d; }

    void setNumSteps(int d) { num_steps = d; }

    void setUseWeightWindow(bool x) { use_weight_window = x; }

    void setWeightWindowWidth(int d) { weight_window_width = d; }

    void setWeightWindowOffset(int d) { weight_window_offset = d; }

    void setMyDebugTime(double d) { _debug_time = d; }

    void multiThreadProcessImages(OfxRectI procWindow) override {
        assert(_dstImg->getPixelComponents() == OFX::ePixelComponentRGBA);

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) break;

            auto dstPix = (float *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                auto px0 = (float) x, py0 = (float) y;
                float px, py, weight;
                float acc = 0.0f;
                float weightSum = 0.0f;

                //bool debug_print = x%200 == 0 && y % 200 == 0; // XXX
                //bool debug_print = x == 600 && y == 400; // XXX
                bool debug_print = false;
                if (debug_print) {
                    printf("--- bake x=%d y=%d t=%lf numsteps=%d www=%d wwo=%d\n", x, y, _debug_time, num_steps, weight_window_width, weight_window_offset); // XXX
                }

                weight = getStepWeight(0);
                acc += weight * sampleRandomData(px0, py0);
                weightSum += weight;
                float ux = sampleImageData(vectorXImg_, px0, py0);
                float uy = sampleImageData(vectorYImg_, px0, py0);
                float ux_initial = ux, uy_initial = uy;
                float ux_last = ux_initial, uy_last = uy_initial;
                bool use_last = false;

                if (debug_print) {
                    printf("initial weight=%.3f ux_initial=%.3f uy_initial=%.3f\n", weight, ux, uy); // XXX
                }

                if (ux != 0 || uy != 0) {
                    // integrate forward
                    px = px0, py = py0;
                    for (int i = 0; i < num_steps; i++) {
                        if (!use_last) {
                            ux = sampleImageData(vectorXImg_, px, py);
                            uy = sampleImageData(vectorYImg_, px, py);
                        } else {
                            ux = ux_last;
                            uy = uy_last;
                        }

                        // normalize
                        float umag = sqrtf(ux * ux + uy * uy);
                        ux /= umag;
                        uy /= umag;

                        if (std::isnan(ux) || std::isnan(uy) || (ux == 0 && uy == 0)) {
                            // we're out of picture / out of area where vectors are defined,
                            // so we will imagine that the vector field goes in the last known direction to infinity
                            use_last = true;
                            ux = ux_last;
                            uy = uy_last;
                        }

                        px += ux;
                        py += uy;
                        float value = sampleRandomData(px, py);
                        weight = getStepWeight(i+1);
                        acc += weight * value;
                        weightSum += weight;
                        ux_last = ux;
                        uy_last = uy;

                        if (debug_print) {
                            printf("step %+d weight=%.3f ux=%.3f uy=%.3f px=%.3f py=%.3f\n", i+1, weight, ux, uy, px, py); // XXX
                        }
                    }

                    // integrate backward
                    px = px0, py = py0;
                    ux_last = ux_initial, uy_last = uy_initial;
                    use_last = false;
                    for (int i = 0; i < num_steps; i++) {
                        if (!use_last) {
                            ux = sampleImageData(vectorXImg_, px, py);
                            uy = sampleImageData(vectorYImg_, px, py);
                        } else {
                            ux = ux_last;
                            uy = uy_last;
                        }

                        // normalize
                        float umag = sqrtf(ux * ux + uy * uy);
                        ux /= umag;
                        uy /= umag;

                        if (std::isnan(ux) || std::isnan(uy) || (ux == 0 && uy == 0)) {
                            // we're out of picture / out of area where vectors are defined,
                            // so we will imagine that the vector field goes in the last known direction to infinity
                            use_last = true;
                            ux = ux_last;
                            uy = uy_last;
                        }

                        px -= ux;
                        py -= uy;
                        float value = sampleRandomData(px, py);
                        weight = getStepWeight(-i-1);
                        acc += weight * value;
                        weightSum += weight;
                        ux_last = ux;
                        uy_last = uy;

                        if (debug_print) {
                            printf("step %+d weight=%.3f ux=%.3f uy=%.3f px=%.3f py=%.3f\n", -i-1, weight, ux, uy, px, py); // XXX
                        }
                    }
                } else {
                    // we're starting at a null vector; no point in integrating,
                    // treat this the same as NaN and mask this pixel in output
                    if (debug_print) {
                        printf("special case - starting at null vector!\n"); // XXX
                    }
                    acc = 0.0f;
                    weightSum = 0.0f;
                }

                float value = acc / weightSum;
                float alpha = 1.0f;
                if (weightSum < 0.5f) {
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
    OFX::BooleanParam *use_weight_window_;
    OFX::IntParam *weight_window_width_;
    OFX::IntParam *weight_window_offset_;

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
        use_weight_window_ = fetchBooleanParam("use_weight_window");
        weight_window_width_ = fetchIntParam("weight_window_width");
        weight_window_offset_ = fetchIntParam("weight_window_offset");
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
    bool use_weight_window = use_weight_window_->getValueAtTime(args.time);
    int weight_window_width = weight_window_width_->getValueAtTime(args.time);
    int weight_window_offset = weight_window_offset_->getValueAtTime(args.time);

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
    processor.setUseWeightWindow(use_weight_window);
    processor.setWeightWindowWidth(weight_window_width);
    processor.setWeightWindowOffset(weight_window_offset);
    processor.setMyDebugTime(args.time);

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

    auto *use_weight_window = desc.defineBooleanParam("use_weight_window");
    use_weight_window->setLabels("use_weight_window", "Hanning window", "Use weight window");
    use_weight_window->setScriptName("use_weight_window");
    use_weight_window->setHint("weight line integral by linear falloff");
    use_weight_window->setDefault(false);

    auto *weight_window_width = desc.defineIntParam("weight_window_width");
    weight_window_width->setLabels("weight_window_width", "Hann.w. width", "weight window half-width");
    weight_window_width->setScriptName("weight_window_width");
    weight_window_width->setHint("half-width of the window (in steps) - use this for animation");
    weight_window_width->setDefault(5);
    weight_window_width->setRange(3, 50);
    weight_window_width->setDisplayRange(3, 50);

    auto *weight_window_offset = desc.defineIntParam("weight_window_offset");
    weight_window_offset->setLabels("weight_window_offset", "Hann.w. offset", "weight window offset");
    weight_window_offset->setScriptName("weight_window_offset");
    weight_window_offset->setHint("offset of center for the window (in steps) - use this for animation");
    weight_window_offset->setDefault(0);
    weight_window_offset->setRange(-10000, 10000);
    weight_window_offset->setDisplayRange(-100, 100);
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
