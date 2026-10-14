// EarComp Audio Unit (v2 component): wraps the Processor for hosts like SoundSource.
#include <AudioUnitSDK/AUEffectBase.h>
#include "MimiTables.h"
#include "Processor.hpp"

namespace {

enum : AudioUnitParameterID { kParamIntensity = 0, kParamVariant = 1, kParamRightTreble = 2 };

struct PresetValues {
    float intensity, variant, rightTreble;
};
AUPreset kPresets[] = {
    {0, CFSTR("Mimi match")},
    {1, CFSTR("Right ear +6 dB")},
    {2, CFSTR("Right ear +10 dB")},
};
constexpr PresetValues kPresetValues[] = {
    {100.0f, 0.0f, 0.0f},
    {100.0f, 0.0f, 6.0f},
    {100.0f, 0.0f, 10.0f},
};
constexpr int kNumPresets = sizeof(kPresetValues) / sizeof(kPresetValues[0]);

} // namespace

class EarCompAU : public ausdk::AUEffectBase {
public:
    explicit EarCompAU(AudioComponentInstance ci) : AUEffectBase(ci, true) {
        applyPreset(0);
        SetAFactoryPresetAsCurrent(kPresets[0]);
    }

    OSStatus Initialize() override {
        OSStatus err = AUEffectBase::Initialize();
        if (err == noErr) processor_.prepare(GetSampleRate());
        return err;
    }

    OSStatus Reset(AudioUnitScope scope, AudioUnitElement element) override {
        processor_.reset();
        return AUEffectBase::Reset(scope, element);
    }

    UInt32 SupportedNumChannels(const AUChannelInfo** outInfo) override {
        static const AUChannelInfo info[] = {{2, 2}};
        if (outInfo) *outInfo = info;
        return 1;
    }

    Float64 GetLatency() noexcept AUSDK_RTSAFE override { return earcomp::Processor::kLatency / GetSampleRate(); }
    Float64 GetTailTime() noexcept AUSDK_RTSAFE override { return earcomp::Processor::kLatency / GetSampleRate(); }
    bool SupportsTail() noexcept AUSDK_RTSAFE override { return true; }

    OSStatus GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id, AudioUnitParameterInfo& info) override {
        if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidParameter;
        info.flags = kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable;
        switch (id) {
        case kParamIntensity:
            FillInParameterName(info, CFSTR("Intensity"), false);
            info.unit = kAudioUnitParameterUnit_Percent;
            info.minValue = 0.0f;
            info.maxValue = 100.0f;
            info.defaultValue = 100.0f;
            return noErr;
        case kParamVariant:
            FillInParameterName(info, CFSTR("Variant"), false);
            info.unit = kAudioUnitParameterUnit_Indexed;
            info.minValue = 0.0f;
            info.maxValue = float(earcomp::kTableVariants - 1);
            info.defaultValue = 0.0f;
            return noErr;
        case kParamRightTreble:
            FillInParameterName(info, CFSTR("Right ear treble"), false);
            info.unit = kAudioUnitParameterUnit_Decibels;
            info.minValue = 0.0f;
            info.maxValue = 12.0f;
            info.defaultValue = 0.0f;
            return noErr;
        default:
            return kAudioUnitErr_InvalidParameter;
        }
    }

    OSStatus GetParameterValueStrings(AudioUnitScope scope, AudioUnitParameterID id, CFArrayRef* outStrings) override {
        if (scope != kAudioUnitScope_Global || id != kParamVariant) return kAudioUnitErr_InvalidProperty;
        if (!outStrings) return noErr;  // host only asks whether strings exist
        CFStringRef names[earcomp::kTableVariants];
        for (int i = 0; i < earcomp::kTableVariants; ++i)
            names[i] = CFStringCreateWithCString(nullptr, earcomp::kVariantNames[i], kCFStringEncodingUTF8);
        *outStrings = CFArrayCreate(nullptr, reinterpret_cast<const void**>(names), earcomp::kTableVariants, &kCFTypeArrayCallBacks);
        for (CFStringRef s : names) CFRelease(s);
        return noErr;
    }

    OSStatus GetPresets(CFArrayRef* outData) const override {
        if (!outData) return noErr;
        CFMutableArrayRef presets = CFArrayCreateMutable(nullptr, kNumPresets, nullptr);
        for (auto& p : kPresets) CFArrayAppendValue(presets, &p);
        *outData = presets;
        return noErr;
    }

    OSStatus NewFactoryPresetSet(const AUPreset& preset) override {
        if (preset.presetNumber < 0 || preset.presetNumber >= kNumPresets) return kAudioUnitErr_InvalidProperty;
        applyPreset(preset.presetNumber);
        SetAFactoryPresetAsCurrent(kPresets[preset.presetNumber]);
        return noErr;
    }

    OSStatus ProcessBufferLists(AudioUnitRenderActionFlags& flags, const AudioBufferList& in,
                                AudioBufferList& out, UInt32 frames) noexcept AUSDK_RTSAFE override {
        if (in.mNumberBuffers < 2 || out.mNumberBuffers < 2) return kAudioUnitErr_FormatNotSupported;
        processor_.intensity.store(GetParameter(kParamIntensity) / 100.0f, std::memory_order_relaxed);
        processor_.variant.store(int(GetParameter(kParamVariant) + 0.5f), std::memory_order_relaxed);
        processor_.rightTrebleDb.store(GetParameter(kParamRightTreble), std::memory_order_relaxed);
        processor_.process(static_cast<const float*>(in.mBuffers[0].mData), static_cast<const float*>(in.mBuffers[1].mData),
                           static_cast<float*>(out.mBuffers[0].mData), static_cast<float*>(out.mBuffers[1].mData), int(frames));
        flags &= ~kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }

private:
    void applyPreset(int n) {
        Globals()->SetParameter(kParamIntensity, kPresetValues[n].intensity);
        Globals()->SetParameter(kParamVariant, kPresetValues[n].variant);
        Globals()->SetParameter(kParamRightTreble, kPresetValues[n].rightTreble);
    }

    earcomp::Processor processor_;
};

AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, EarCompAU)
