//
// Copyright 2017-2023 Valve Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef STEAMAUDIO_API_INTERFACES_H
#define STEAMAUDIO_API_INTERFACES_H

#include "phonon.h"

namespace api {

class IContext;
class ISerializedObject;
class IEmbreeDevice;
class IOpenCLDeviceList;
class IOpenCLDevice;
class IRadeonRaysDevice;
class ITrueAudioNextDevice;
class IScene;
class IStaticMesh;
class IInstancedMesh;
class IHRTF;
class IPanningEffect;
class IBinauralEffect;
class IVirtualSurroundEffect;
class IAmbisonicsEncodeEffect;
class IAmbisonicsPanningEffect;
class IAmbisonicsBinauralEffect;
class IAmbisonicsRotationEffect;
class IAmbisonicsDecodeEffect;
class IDirectEffect;
class IReflectionEffect;
class IReflectionMixer;
class IPathEffect;
class IProbeArray;
class IProbeBatch;
class ISimulator;
class ISource;
class IEnergyField;
class IImpulseResponse;
class IReconstructor;

class IContext
{
public:
    virtual IContext* retain() = 0;

    virtual void release() = 0;

    virtual void setProfilerContext(void* profilerContext) = 0;

    virtual IPLVector3 calculateRelativeDirection(IPLVector3 sourcePosition,
                                                  IPLVector3 listenerPosition,
                                                  IPLVector3 listenerAhead,
                                                  IPLVector3 listenerUp) = 0;

    virtual IPLerror createSerializedObject(IPLSerializedObjectSettings* settings,
                                            ISerializedObject** serializedObject) = 0;

    virtual IPLerror createEmbreeDevice(IPLEmbreeDeviceSettings* settings,
                                        IEmbreeDevice** device) = 0;

    virtual IPLerror createOpenCLDeviceList(IPLOpenCLDeviceSettings* settings,
                                            IOpenCLDeviceList** deviceList) = 0;

    virtual IPLerror createOpenCLDevice(IOpenCLDeviceList* deviceList,
                                        IPLint32 index,
                                        IOpenCLDevice** device) = 0;

    virtual IPLerror createOpenCLDeviceFromExisting(void* convolutionQueue,
                                                    void* irUpdateQueue,
                                                    IOpenCLDevice** device) = 0;

    virtual IPLerror createScene(IPLSceneSettings* settings,
                                 IScene** scene) = 0;

    virtual IPLerror loadScene(IPLSceneSettings* settings,
                               ISerializedObject* serializedObject,
                               IPLProgressCallback progressCallback,
                               void* userData,
                               IScene** scene) = 0;

    virtual IPLerror allocateAudioBuffer(IPLint32 numChannels,
                                         IPLint32 numSamples,
                                         IPLAudioBuffer* audioBuffer) = 0;

    virtual void freeAudioBuffer(IPLAudioBuffer* audioBuffer) = 0;

    virtual void interleaveAudioBuffer(IPLAudioBuffer* src,
                                       IPLfloat32* dst) = 0;

    virtual void deinterleaveAudioBuffer(IPLfloat32* src,
                                         IPLAudioBuffer* dst) = 0;

    virtual void mixAudioBuffer(IPLAudioBuffer* in,
                                IPLAudioBuffer* mix) = 0;

    virtual void downmixAudioBuffer(IPLAudioBuffer* in,
                                    IPLAudioBuffer* out) = 0;

    virtual void convertAmbisonicAudioBuffer(IPLAmbisonicsType inType,
                                             IPLAmbisonicsType outType,
                                             IPLAudioBuffer* in,
                                             IPLAudioBuffer* out) = 0;

    virtual IPLerror createHRTF(IPLAudioSettings* audioSettings,
                                IPLHRTFSettings* hrtfSettings,
                                IHRTF** hrtf) = 0;

    virtual IPLerror createPanningEffect(IPLAudioSettings* audioSettings,
                                         IPLPanningEffectSettings* effectSettings,
                                         IPanningEffect** effect) = 0;

    virtual IPLerror createBinauralEffect(IPLAudioSettings* audioSettings,
                                          IPLBinauralEffectSettings* effectSettings,
                                          IBinauralEffect** effect) = 0;

    virtual IPLerror createVirtualSurroundEffect(IPLAudioSettings* audioSettings,
                                                 IPLVirtualSurroundEffectSettings* effectSettings,
                                                 IVirtualSurroundEffect** effect) = 0;

    virtual IPLerror createAmbisonicsEncodeEffect(IPLAudioSettings* audioSettings,
                                                  IPLAmbisonicsEncodeEffectSettings* effectSettings,
                                                  IAmbisonicsEncodeEffect** effect) = 0;

    virtual IPLerror createAmbisonicsPanningEffect(IPLAudioSettings* audioSettings,
                                                   IPLAmbisonicsPanningEffectSettings* effectSettings,
                                                   IAmbisonicsPanningEffect** effect) = 0;

    virtual IPLerror createAmbisonicsBinauralEffect(IPLAudioSettings* audioSettings,
                                                    IPLAmbisonicsBinauralEffectSettings* effectSettings,
                                                    IAmbisonicsBinauralEffect** effect) = 0;

    virtual IPLerror createAmbisonicsRotationEffect(IPLAudioSettings* audioSettings,
                                                    IPLAmbisonicsRotationEffectSettings* effectSettings,
                                                    IAmbisonicsRotationEffect** effect) = 0;

    virtual IPLerror createAmbisonicsDecodeEffect(IPLAudioSettings* audioSettings,
                                                  IPLAmbisonicsDecodeEffectSettings* effectSettings,
                                                  IAmbisonicsDecodeEffect** effect) = 0;

    virtual IPLerror createDirectEffect(IPLAudioSettings* audioSettings,
                                        IPLDirectEffectSettings* effectSettings,
                                        IDirectEffect** effect) = 0;

    virtual IPLerror createReflectionEffect(IPLAudioSettings* audioSettings,
                                            IPLReflectionEffectSettings* effectSettings,
                                            IReflectionEffect** effect) = 0;

    virtual IPLerror createReflectionMixer(IPLAudioSettings* audioSettings,
                                           IPLReflectionEffectSettings* effectSettings,
                                           IReflectionMixer** mixer) = 0;

    virtual IPLerror createPathEffect(IPLAudioSettings* audioSettings,
                                      IPLPathEffectSettings* effectSettings,
                                      IPathEffect** effect) = 0;

    virtual IPLerror createProbeArray(IProbeArray** probeArray) = 0;

    virtual IPLerror createProbeBatch(IProbeBatch** probeBatch) = 0;

    virtual IPLerror loadProbeBatch(ISerializedObject* serializedObject,
                                    IProbeBatch** probeBatch) = 0;

    virtual void bakeReflections(IPLReflectionsBakeParams* params,
                                 IPLProgressCallback progressCallback,
                                 void* userData) = 0;

    virtual void cancelBakeReflections() = 0;

    virtual void bakePaths(IPLPathBakeParams* params,
                           IPLProgressCallback progressCallback,
                           void* userData) = 0;

    virtual void cancelBakePaths() = 0;

    virtual IPLerror createSimulator(IPLSimulationSettings* settings,
                                     ISimulator** simulator) = 0;

    virtual IPLfloat32 calculateDistanceAttenuation(IPLVector3 source,
                                                    IPLVector3 listener,
                                                    IPLDistanceAttenuationModel* model) = 0;

    virtual void calculateAirAbsorption(IPLVector3 source,
                                        IPLVector3 listener,
                                        IPLAirAbsorptionModel* model,
                                        IPLfloat32* airAbsorption) = 0;

    virtual IPLfloat32 calculateDirectivity(IPLCoordinateSpace3 source,
                                            IPLVector3 listener,
                                            IPLDirectivity* model) = 0;

    virtual IPLerror createEnergyField(const IPLEnergyFieldSettings* settings,
                                       IEnergyField** energyField) = 0;

    virtual IPLerror createImpulseResponse(const IPLImpulseResponseSettings* settings,
                                           IImpulseResponse** impulseResponse) = 0;

    virtual IPLerror createReconstructor(const IPLReconstructorSettings* settings,
                                         IReconstructor** reconstructor) = 0;
};

class ISerializedObject
{
public:
    virtual ISerializedObject* retain() = 0;

    virtual void release() = 0;

    virtual IPLsize getSize() = 0;

    virtual IPLbyte* getData() = 0;
};

class IEmbreeDevice
{
public:
    virtual IEmbreeDevice* retain() = 0;

    virtual void release() = 0;
};

class IOpenCLDeviceList
{
public:
    virtual IOpenCLDeviceList* retain() = 0;

    virtual void release() = 0;

    virtual IPLint32 getNumDevices() = 0;

    virtual void getDeviceDesc(IPLint32 index,
                               IPLOpenCLDeviceDesc* deviceDesc) = 0;
};

class IOpenCLDevice
{
public:
    virtual IOpenCLDevice* retain() = 0;

    virtual void release() = 0;

    virtual IPLerror createRadeonRaysDevice(IPLRadeonRaysDeviceSettings* settings,
                                            IRadeonRaysDevice** device) = 0;

    virtual IPLerror createTrueAudioNextDevice(IPLTrueAudioNextDeviceSettings* settings,
                                               ITrueAudioNextDevice** device) = 0;
};

class IRadeonRaysDevice
{
public:
    virtual IRadeonRaysDevice* retain() = 0;

    virtual void release() = 0;
};

class ITrueAudioNextDevice
{
public:
    virtual ITrueAudioNextDevice* retain() = 0;

    virtual void release() = 0;
};

class IStaticMesh;

class IScene
{
public:
    virtual IScene* retain() = 0;

    virtual void release() = 0;

    virtual void save(ISerializedObject* serializedObject) = 0;

    virtual void saveOBJ(IPLstring fileBaseName) = 0;

    virtual void commit() = 0;

    virtual IPLerror createStaticMesh(IPLStaticMeshSettings* settings,
                                      IStaticMesh** staticMesh) = 0;

    virtual IPLerror loadStaticMesh(ISerializedObject* serializedObject,
                                    IPLProgressCallback progressCallback,
                                    void* userData,
                                    IStaticMesh** staticMesh) = 0;

    virtual IPLerror createInstancedMesh(IPLInstancedMeshSettings* settings,
                                         IInstancedMesh** instancedMesh) = 0;

    virtual void setStaticMeshMaterial(IStaticMesh* staticMesh, IPLMaterial* newMaterial, IPLint32 index) = 0;
};

class IStaticMesh
{
public:
    virtual IStaticMesh* retain() = 0;

    virtual void release() = 0;

    virtual void save(ISerializedObject* serializedObject) = 0;

    virtual void add(IScene* scene) = 0;

    virtual void remove(IScene* scene) = 0;
};

class IInstancedMesh
{
public:
    virtual IInstancedMesh* retain() = 0;

    virtual void release() = 0;

    virtual void add(IScene* scene) = 0;

    virtual void remove(IScene* scene) = 0;

    virtual void updateTransform(IScene* scene,
                                 IPLMatrix4x4 transform) = 0;
};

class IHRTF
{
public:
    virtual IHRTF* retain() = 0;

    virtual void release() = 0;
};

class IPanningEffect
{
public:
    virtual IPanningEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLPanningEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IBinauralEffect
{
public:
    virtual IBinauralEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLBinauralEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IVirtualSurroundEffect
{
public:
    virtual IVirtualSurroundEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLVirtualSurroundEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IAmbisonicsEncodeEffect
{
public:
    virtual IAmbisonicsEncodeEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLAmbisonicsEncodeEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IAmbisonicsPanningEffect
{
public:
    virtual IAmbisonicsPanningEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLAmbisonicsPanningEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IAmbisonicsBinauralEffect
{
public:
    virtual IAmbisonicsBinauralEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLAmbisonicsBinauralEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IAmbisonicsRotationEffect
{
public:
    virtual IAmbisonicsRotationEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLAmbisonicsRotationEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IAmbisonicsDecodeEffect
{
public:
    virtual IAmbisonicsDecodeEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLAmbisonicsDecodeEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IDirectEffect
{
public:
    virtual IDirectEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLDirectEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IReflectionEffect
{
public:
    virtual IReflectionEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLReflectionEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out,
                                      IReflectionMixer* mixer) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out,
                                        IReflectionMixer* mixer) = 0;
};

class IReflectionMixer
{
public:
    virtual IReflectionMixer* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLReflectionEffectParams* params,
                                      IPLAudioBuffer* out) = 0;
};

class IPathEffect
{
public:
    virtual IPathEffect* retain() = 0;

    virtual void release() = 0;

    virtual void reset() = 0;

    virtual IPLAudioEffectState apply(IPLPathEffectParams* params,
                                      IPLAudioBuffer* in,
                                      IPLAudioBuffer* out) = 0;

    virtual IPLint32 getTailSize() = 0;

    virtual IPLAudioEffectState getTail(IPLAudioBuffer* out) = 0;
};

class IProbeArray
{
public:
    virtual IProbeArray* retain() = 0;

    virtual void release() = 0;

    virtual void generateProbes(IScene* scene,
                                IPLProbeGenerationParams* params) = 0;

    virtual IPLint32 getNumProbes() = 0;

    virtual IPLSphere getProbe(IPLint32 index) = 0;
};

class IProbeBatch
{
public:
    virtual IProbeBatch* retain() = 0;

    virtual void release() = 0;

    virtual void save(ISerializedObject* serializedObject) = 0;

    virtual IPLint32 getNumProbes() = 0;

    virtual void addProbe(IPLSphere probe) = 0;

    virtual void addProbeArray(IProbeArray* probeArray) = 0;

    virtual void removeProbe(IPLint32 index) = 0;

    virtual void commit() = 0;

    virtual void removeData(IPLBakedDataIdentifier* identifier) = 0;

    virtual IPLsize getDataSize(IPLBakedDataIdentifier* identifier) = 0;

    virtual void getEnergyField(IPLBakedDataIdentifier* identifier, int probeIndex, IEnergyField* energyField) = 0;

    virtual void getReverb(IPLBakedDataIdentifier* identifier, int probeIndex, float* reverbTimes) = 0;
};

class ISimulator
{
public:
    virtual ISimulator* retain() = 0;

    virtual void release() = 0;

    virtual void setScene(IScene* scene) = 0;

    virtual void addProbeBatch(IProbeBatch* probeBatch) = 0;

    virtual void removeProbeBatch(IProbeBatch* probeBatch) = 0;

    virtual void setSharedInputs(IPLSimulationFlags flags,
                                 IPLSimulationSharedInputs* sharedInputs) = 0;

    virtual void commit() = 0;

    virtual void runDirect() = 0;

    virtual void runReflections() = 0;

    virtual void runPathing() = 0;

    virtual IPLerror createSource(IPLSourceSettings* settings,
                                  ISource** source) = 0;
};

class ISource
{
public:
    virtual ISource* retain() = 0;

    virtual void release() = 0;

    virtual void add(ISimulator* simulator) = 0;

    virtual void remove(ISimulator* simulator) = 0;

    virtual void setInputs(IPLSimulationFlags flags,
                           IPLSimulationInputs* inputs) = 0;

    virtual void getOutputs(IPLSimulationFlags flags,
                            IPLSimulationOutputs* outputs) = 0;
};

class IEnergyField
{
public:
    virtual IEnergyField* retain() = 0;

    virtual void release() = 0;

    virtual int getNumChannels() = 0;

    virtual int getNumBins() = 0;

    virtual float* getData() = 0;

    virtual float* getChannel(int channelIndex) = 0;

    virtual float* getBand(int channelIndex, int bandIndex) = 0;

    virtual void reset() = 0;

    virtual void copy(IEnergyField* src) = 0;

    virtual void swap(IEnergyField* other) = 0;

    virtual void add(IEnergyField* in1, IEnergyField* in2) = 0;

    virtual void scale(IEnergyField* in, float scalar) = 0;

    virtual void scaleAccum(IEnergyField* in, float scalar) = 0;
};

class IImpulseResponse
{
public:
    virtual IImpulseResponse* retain() = 0;

    virtual void release() = 0;

    virtual int getNumChannels() = 0;

    virtual int getNumSamples() = 0;

    virtual float* getData() = 0;

    virtual float* getChannel(int channelIndex) = 0;

    virtual void reset() = 0;

    virtual void copy(IImpulseResponse* src) = 0;

    virtual void swap(IImpulseResponse* other) = 0;

    virtual void add(IImpulseResponse* in1, IImpulseResponse* in2) = 0;

    virtual void scale(IImpulseResponse* in, float scalar) = 0;

    virtual void scaleAccum(IImpulseResponse* in, float scalar) = 0;
};

class IReconstructor
{
public:
    virtual IReconstructor* retain() = 0;

    virtual void release() = 0;

    virtual void reconstruct(IPLint32 numInputs, const IPLReconstructorInputs* inputs,
                             const IPLReconstructorSharedInputs* sharedInputs, IPLReconstructorOutputs* outputs) = 0;
};

}

#if !defined(STEAMAUDIO_SKIP_API_FUNCTIONS)

// [godot] Test-only fault-injection seam (see patches/0001): when non-null it is
// invoked inside iplContextRetain's guarded region, letting tests drive a foreign
// (non-ipl::Exception) exception through the hardened catch(...) boundary below.
// Defined (as null) in api_context.cpp; production code never sets it.
extern "C" void (*ipl_godot_boundary_fault_hook)();

#if !defined(STEAMAUDIO_BUILDING_CORE)
IPLerror IPLCALL iplContextCreate(IPLContextSettings* settings,
                          IPLContext* context)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        return IPL_STATUS_FAILURE;
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}
#endif

IPLContext IPLCALL iplContextRetain(IPLContext context)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (ipl_godot_boundary_fault_hook)
            ipl_godot_boundary_fault_hook();

        if (!context)
            return nullptr;

        return reinterpret_cast<IPLContext>(reinterpret_cast<api::IContext*>(context)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplContextRelease(IPLContext* context)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context || !*context)
            return;

        reinterpret_cast<api::IContext*>(*context)->release();

        *context = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLVector3 IPLCALL iplCalculateRelativeDirection(IPLContext context,
                                         IPLVector3 sourcePosition,
                                         IPLVector3 listenerPosition,
                                         IPLVector3 listenerAhead,
                                         IPLVector3 listenerUp)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPLVector3{};

        return reinterpret_cast<api::IContext*>(context)->calculateRelativeDirection(sourcePosition, listenerPosition, listenerAhead, listenerUp);
    }
    catch (...)
    {
        return IPLVector3{};
    }
}

IPLerror IPLCALL iplSerializedObjectCreate(IPLContext context,
                                   IPLSerializedObjectSettings* settings,
                                   IPLSerializedObject* serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createSerializedObject(settings, reinterpret_cast<api::ISerializedObject**>(serializedObject));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLSerializedObject IPLCALL iplSerializedObjectRetain(IPLSerializedObject serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!serializedObject)
            return nullptr;

        return reinterpret_cast<IPLSerializedObject>(reinterpret_cast<api::ISerializedObject*>(serializedObject)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplSerializedObjectRelease(IPLSerializedObject* serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!serializedObject || !*serializedObject)
            return;

        reinterpret_cast<api::ISerializedObject*>(*serializedObject)->release();

        *serializedObject = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLsize IPLCALL iplSerializedObjectGetSize(IPLSerializedObject serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!serializedObject)
            return 0;

        return reinterpret_cast<api::ISerializedObject*>(serializedObject)->getSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLbyte* IPLCALL iplSerializedObjectGetData(IPLSerializedObject serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!serializedObject)
            return 0;

        return reinterpret_cast<api::ISerializedObject*>(serializedObject)->getData();
    }
    catch (...)
    {
        return nullptr;
    }
}

IPLerror IPLCALL iplEmbreeDeviceCreate(IPLContext context,
                               IPLEmbreeDeviceSettings* settings,
                               IPLEmbreeDevice* device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createEmbreeDevice(settings, reinterpret_cast<api::IEmbreeDevice**>(device));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLEmbreeDevice IPLCALL iplEmbreeDeviceRetain(IPLEmbreeDevice device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device)
            return nullptr;

        return reinterpret_cast<IPLEmbreeDevice>(reinterpret_cast<api::IEmbreeDevice*>(device)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplEmbreeDeviceRelease(IPLEmbreeDevice* device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device || !*device)
            return;

        reinterpret_cast<api::IEmbreeDevice*>(*device)->release();

        *device = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplOpenCLDeviceListCreate(IPLContext context,
                                   IPLOpenCLDeviceSettings* settings,
                                   IPLOpenCLDeviceList* deviceList)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createOpenCLDeviceList(settings, reinterpret_cast<api::IOpenCLDeviceList**>(deviceList));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLOpenCLDeviceList IPLCALL iplOpenCLDeviceListRetain(IPLOpenCLDeviceList deviceList)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!deviceList)
            return nullptr;

        return reinterpret_cast<IPLOpenCLDeviceList>(reinterpret_cast<api::IOpenCLDeviceList*>(deviceList)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplOpenCLDeviceListRelease(IPLOpenCLDeviceList* deviceList)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!deviceList || !*deviceList)
            return;

        reinterpret_cast<api::IOpenCLDeviceList*>(*deviceList)->release();

        *deviceList = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLint32 IPLCALL iplOpenCLDeviceListGetNumDevices(IPLOpenCLDeviceList deviceList)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!deviceList)
            return 0;

        return reinterpret_cast<api::IOpenCLDeviceList*>(deviceList)->getNumDevices();
    }
    catch (...)
    {
        return 0;
    }
}

void IPLCALL iplOpenCLDeviceListGetDeviceDesc(IPLOpenCLDeviceList deviceList,
                                      IPLint32 index,
                                      IPLOpenCLDeviceDesc* deviceDesc)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!deviceList)
            return;

        reinterpret_cast<api::IOpenCLDeviceList*>(deviceList)->getDeviceDesc(index, deviceDesc);
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplOpenCLDeviceCreate(IPLContext context,
                               IPLOpenCLDeviceList deviceList,
                               IPLint32 index,
                               IPLOpenCLDevice* device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createOpenCLDevice(reinterpret_cast<api::IOpenCLDeviceList*>(deviceList), index, reinterpret_cast<api::IOpenCLDevice**>(device));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLerror IPLCALL iplOpenCLDeviceCreateFromExisting(IPLContext context,
                                           void* convolutionQueue,
                                           void* irUpdateQueue,
                                           IPLOpenCLDevice* device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createOpenCLDeviceFromExisting(convolutionQueue, irUpdateQueue, reinterpret_cast<api::IOpenCLDevice**>(device));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLOpenCLDevice IPLCALL iplOpenCLDeviceRetain(IPLOpenCLDevice device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device)
            return nullptr;

        return reinterpret_cast<IPLOpenCLDevice>(reinterpret_cast<api::IOpenCLDevice*>(device)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplOpenCLDeviceRelease(IPLOpenCLDevice* device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device || !*device)
            return;

        reinterpret_cast<api::IOpenCLDevice*>(*device)->release();

        *device = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplRadeonRaysDeviceCreate(IPLOpenCLDevice openCLDevice,
                                   IPLRadeonRaysDeviceSettings* settings,
                                   IPLRadeonRaysDevice* rrDevice)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!openCLDevice)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IOpenCLDevice*>(openCLDevice)->createRadeonRaysDevice(settings, reinterpret_cast<api::IRadeonRaysDevice**>(rrDevice));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLRadeonRaysDevice IPLCALL iplRadeonRaysDeviceRetain(IPLRadeonRaysDevice device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device)
            return nullptr;

        return reinterpret_cast<IPLRadeonRaysDevice>(reinterpret_cast<api::IRadeonRaysDevice*>(device)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplRadeonRaysDeviceRelease(IPLRadeonRaysDevice* device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device || !*device)
            return;

        reinterpret_cast<api::IRadeonRaysDevice*>(*device)->release();

        *device = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplTrueAudioNextDeviceCreate(IPLOpenCLDevice openCLDevice,
                                      IPLTrueAudioNextDeviceSettings* settings,
                                      IPLTrueAudioNextDevice* tanDevice)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!openCLDevice)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IOpenCLDevice*>(openCLDevice)->createTrueAudioNextDevice(settings, reinterpret_cast<api::ITrueAudioNextDevice**>(tanDevice));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLTrueAudioNextDevice IPLCALL iplTrueAudioNextDeviceRetain(IPLTrueAudioNextDevice device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device)
            return nullptr;

        return reinterpret_cast<IPLTrueAudioNextDevice>(reinterpret_cast<api::ITrueAudioNextDevice*>(device)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplTrueAudioNextDeviceRelease(IPLTrueAudioNextDevice* device)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!device || !*device)
            return;

        reinterpret_cast<api::ITrueAudioNextDevice*>(*device)->release();

        *device = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplSceneCreate(IPLContext context,
                        IPLSceneSettings* settings,
                        IPLScene* scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createScene(settings, reinterpret_cast<api::IScene**>(scene));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLScene IPLCALL iplSceneRetain(IPLScene scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene)
            return nullptr;

        return reinterpret_cast<IPLScene>(reinterpret_cast<api::IScene*>(scene)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplSceneRelease(IPLScene* scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene || !*scene)
            return;

        reinterpret_cast<api::IScene*>(*scene)->release();

        *scene = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplSceneLoad(IPLContext context,
                      IPLSceneSettings* settings,
                      IPLSerializedObject serializedObject,
                      IPLProgressCallback progressCallback,
                      void* progressCallbackUserData,
                      IPLScene* scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->loadScene(settings, reinterpret_cast<api::ISerializedObject*>(serializedObject),
                                                                    progressCallback, progressCallbackUserData,
                                                                    reinterpret_cast<api::IScene**>(scene));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

void IPLCALL iplSceneSave(IPLScene scene,
                  IPLSerializedObject serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene)
            return;

        reinterpret_cast<api::IScene*>(scene)->save(reinterpret_cast<api::ISerializedObject*>(serializedObject));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSceneSaveOBJ(IPLScene scene,
                     IPLstring fileBaseName)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene)
            return;

        reinterpret_cast<api::IScene*>(scene)->saveOBJ(fileBaseName);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSceneCommit(IPLScene scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene)
            return;

        reinterpret_cast<api::IScene*>(scene)->commit();
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplStaticMeshCreate(IPLScene scene,
                             IPLStaticMeshSettings* settings,
                             IPLStaticMesh* staticMesh)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IScene*>(scene)->createStaticMesh(settings, reinterpret_cast<api::IStaticMesh**>(staticMesh));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLStaticMesh IPLCALL iplStaticMeshRetain(IPLStaticMesh staticMesh)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!staticMesh)
            return nullptr;

        return reinterpret_cast<IPLStaticMesh>(reinterpret_cast<api::IStaticMesh*>(staticMesh)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplStaticMeshRelease(IPLStaticMesh* staticMesh)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!staticMesh || !*staticMesh)
            return;

        reinterpret_cast<api::IStaticMesh*>(*staticMesh)->release();

        *staticMesh = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplStaticMeshLoad(IPLScene scene,
                           IPLSerializedObject serializedObject,
                           IPLProgressCallback progressCallback,
                           void* progressCallbackUserData,
                           IPLStaticMesh* staticMesh)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IScene*>(scene)->loadStaticMesh(reinterpret_cast<api::ISerializedObject*>(serializedObject),
                                                                     progressCallback, progressCallbackUserData,
                                                                     reinterpret_cast<api::IStaticMesh**>(staticMesh));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

void IPLCALL iplStaticMeshSave(IPLStaticMesh staticMesh,
                       IPLSerializedObject serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!staticMesh)
            return;

        reinterpret_cast<api::IStaticMesh*>(staticMesh)->save(reinterpret_cast<api::ISerializedObject*>(serializedObject));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplStaticMeshAdd(IPLStaticMesh staticMesh, IPLScene scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!staticMesh)
            return;

        reinterpret_cast<api::IStaticMesh*>(staticMesh)->add(reinterpret_cast<api::IScene*>(scene));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplStaticMeshSetMaterial(IPLStaticMesh staticMesh, IPLScene scene, IPLMaterial* newMaterial, IPLint32 index)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!staticMesh || !scene)
            return;
    
        reinterpret_cast<api::IScene*>(scene)->setStaticMeshMaterial(reinterpret_cast<api::IStaticMesh*>(staticMesh), newMaterial, index);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplStaticMeshRemove(IPLStaticMesh staticMesh, IPLScene scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!staticMesh)
            return;

        reinterpret_cast<api::IStaticMesh*>(staticMesh)->remove(reinterpret_cast<api::IScene*>(scene));
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplInstancedMeshCreate(IPLScene scene,
                                IPLInstancedMeshSettings* settings,
                                IPLInstancedMesh* instancedMesh)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!scene)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IScene*>(scene)->createInstancedMesh(settings, reinterpret_cast<api::IInstancedMesh**>(instancedMesh));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLInstancedMesh IPLCALL iplInstancedMeshRetain(IPLInstancedMesh instancedMesh)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!instancedMesh)
            return nullptr;

        return reinterpret_cast<IPLInstancedMesh>(reinterpret_cast<api::IInstancedMesh*>(instancedMesh)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplInstancedMeshRelease(IPLInstancedMesh* instancedMesh)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!instancedMesh || !*instancedMesh)
            return;

        reinterpret_cast<api::IInstancedMesh*>(*instancedMesh)->release();

        *instancedMesh = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplInstancedMeshAdd(IPLInstancedMesh instancedMesh, IPLScene scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!instancedMesh)
            return;

        reinterpret_cast<api::IInstancedMesh*>(instancedMesh)->add(reinterpret_cast<api::IScene*>(scene));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplInstancedMeshRemove(IPLInstancedMesh instancedMesh, IPLScene scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!instancedMesh)
            return;

        reinterpret_cast<api::IInstancedMesh*>(instancedMesh)->remove(reinterpret_cast<api::IScene*>(scene));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplInstancedMeshUpdateTransform(IPLInstancedMesh instancedMesh, IPLScene scene, IPLMatrix4x4 transform)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!instancedMesh)
            return;

        reinterpret_cast<api::IInstancedMesh*>(instancedMesh)->updateTransform(reinterpret_cast<api::IScene*>(scene), transform);
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplAudioBufferAllocate(IPLContext context,
                                IPLint32 numChannels,
                                IPLint32 numSamples,
                                IPLAudioBuffer* audioBuffer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->allocateAudioBuffer(numChannels, numSamples, audioBuffer);
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

void IPLCALL iplAudioBufferFree(IPLContext context,
                        IPLAudioBuffer* audioBuffer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->freeAudioBuffer(audioBuffer);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAudioBufferInterleave(IPLContext context,
                              IPLAudioBuffer* src,
                              IPLfloat32* dst)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->interleaveAudioBuffer(src, dst);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAudioBufferDeinterleave(IPLContext context,
                                IPLfloat32* src,
                                IPLAudioBuffer* dst)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->deinterleaveAudioBuffer(src, dst);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAudioBufferMix(IPLContext context,
                       IPLAudioBuffer* in,
                       IPLAudioBuffer* mix)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->mixAudioBuffer(in, mix);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAudioBufferDownmix(IPLContext context,
                           IPLAudioBuffer* in,
                           IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->downmixAudioBuffer(in, out);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAudioBufferConvertAmbisonics(IPLContext context,
                                     IPLAmbisonicsType inType,
                                     IPLAmbisonicsType outType,
                                     IPLAudioBuffer* in,
                                     IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->convertAmbisonicAudioBuffer(inType, outType, in, out);
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplHRTFCreate(IPLContext context,
                       IPLAudioSettings* audioSettings,
                       IPLHRTFSettings* hrtfSettings,
                       IPLHRTF* hrtf)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createHRTF(audioSettings, hrtfSettings, reinterpret_cast<api::IHRTF**>(hrtf));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLHRTF IPLCALL iplHRTFRetain(IPLHRTF hrtf)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!hrtf)
            return nullptr;

        return reinterpret_cast<IPLHRTF>(reinterpret_cast<api::IHRTF*>(hrtf)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplHRTFRelease(IPLHRTF* hrtf)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!hrtf || !*hrtf)
            return;

        reinterpret_cast<api::IHRTF*>(*hrtf)->release();

        *hrtf = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplPanningEffectCreate(IPLContext context,
                                IPLAudioSettings* audioSettings,
                                IPLPanningEffectSettings* effectSettings,
                                IPLPanningEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createPanningEffect(audioSettings, effectSettings, reinterpret_cast<api::IPanningEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLPanningEffect IPLCALL iplPanningEffectRetain(IPLPanningEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLPanningEffect>(reinterpret_cast<api::IPanningEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplPanningEffectRelease(IPLPanningEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IPanningEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplPanningEffectReset(IPLPanningEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IPanningEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplPanningEffectApply(IPLPanningEffect effect,
                                          IPLPanningEffectParams* params,
                                          IPLAudioBuffer* in,
                                          IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IPanningEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplPanningEffectGetTailSize(IPLPanningEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IPanningEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplPanningEffectGetTail(IPLPanningEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IPanningEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplBinauralEffectCreate(IPLContext context,
                                 IPLAudioSettings* audioSettings,
                                 IPLBinauralEffectSettings* effectSettings,
                                 IPLBinauralEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createBinauralEffect(audioSettings, effectSettings, reinterpret_cast<api::IBinauralEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLBinauralEffect IPLCALL iplBinauralEffectRetain(IPLBinauralEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLBinauralEffect>(reinterpret_cast<api::IBinauralEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplBinauralEffectRelease(IPLBinauralEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IBinauralEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplBinauralEffectReset(IPLBinauralEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IBinauralEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplBinauralEffectApply(IPLBinauralEffect effect,
                                           IPLBinauralEffectParams* params,
                                           IPLAudioBuffer* in,
                                           IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IBinauralEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplBinauralEffectGetTailSize(IPLBinauralEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IBinauralEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplBinauralEffectGetTail(IPLBinauralEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IBinauralEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplVirtualSurroundEffectCreate(IPLContext context,
                                        IPLAudioSettings* audioSettings,
                                        IPLVirtualSurroundEffectSettings* effectSettings,
                                        IPLVirtualSurroundEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createVirtualSurroundEffect(audioSettings, effectSettings, reinterpret_cast<api::IVirtualSurroundEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLVirtualSurroundEffect IPLCALL iplVirtualSurroundEffectRetain(IPLVirtualSurroundEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLVirtualSurroundEffect>(reinterpret_cast<api::IVirtualSurroundEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplVirtualSurroundEffectRelease(IPLVirtualSurroundEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IVirtualSurroundEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplVirtualSurroundEffectReset(IPLVirtualSurroundEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IVirtualSurroundEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplVirtualSurroundEffectApply(IPLVirtualSurroundEffect effect,
                                                  IPLVirtualSurroundEffectParams* params,
                                                  IPLAudioBuffer* in,
                                                  IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IVirtualSurroundEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplVirtualSurroundEffectGetTailSize(IPLVirtualSurroundEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IVirtualSurroundEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplVirtualSurroundEffectGetTail(IPLVirtualSurroundEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IVirtualSurroundEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplAmbisonicsEncodeEffectCreate(IPLContext context,
                                         IPLAudioSettings* audioSettings,
                                         IPLAmbisonicsEncodeEffectSettings* effectSettings,
                                         IPLAmbisonicsEncodeEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createAmbisonicsEncodeEffect(audioSettings, effectSettings, reinterpret_cast<api::IAmbisonicsEncodeEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLAmbisonicsEncodeEffect IPLCALL iplAmbisonicsEncodeEffectRetain(IPLAmbisonicsEncodeEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLAmbisonicsEncodeEffect>(reinterpret_cast<api::IAmbisonicsEncodeEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplAmbisonicsEncodeEffectRelease(IPLAmbisonicsEncodeEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IAmbisonicsEncodeEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAmbisonicsEncodeEffectReset(IPLAmbisonicsEncodeEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IAmbisonicsEncodeEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsEncodeEffectApply(IPLAmbisonicsEncodeEffect effect,
                                                   IPLAmbisonicsEncodeEffectParams* params,
                                                   IPLAudioBuffer* in,
                                                   IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IAmbisonicsEncodeEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplAmbisonicsEncodeEffectGetTailSize(IPLAmbisonicsEncodeEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IAmbisonicsEncodeEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsEncodeEffectGetTail(IPLAmbisonicsEncodeEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IAmbisonicsEncodeEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplAmbisonicsPanningEffectCreate(IPLContext context,
                                          IPLAudioSettings* audioSettings,
                                          IPLAmbisonicsPanningEffectSettings* effectSettings,
                                          IPLAmbisonicsPanningEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createAmbisonicsPanningEffect(audioSettings, effectSettings, reinterpret_cast<api::IAmbisonicsPanningEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLAmbisonicsPanningEffect IPLCALL iplAmbisonicsPanningEffectRetain(IPLAmbisonicsPanningEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLAmbisonicsPanningEffect>(reinterpret_cast<api::IAmbisonicsPanningEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplAmbisonicsPanningEffectRelease(IPLAmbisonicsPanningEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IAmbisonicsPanningEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAmbisonicsPanningEffectReset(IPLAmbisonicsPanningEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IAmbisonicsPanningEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsPanningEffectApply(IPLAmbisonicsPanningEffect effect,
                                                    IPLAmbisonicsPanningEffectParams* params,
                                                    IPLAudioBuffer* in,
                                                    IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IAmbisonicsPanningEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplAmbisonicsPanningEffectGetTailSize(IPLAmbisonicsPanningEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IAmbisonicsPanningEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsPanningEffectGetTail(IPLAmbisonicsPanningEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IAmbisonicsPanningEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplAmbisonicsBinauralEffectCreate(IPLContext context,
                                           IPLAudioSettings* audioSettings,
                                           IPLAmbisonicsBinauralEffectSettings* effectSettings,
                                           IPLAmbisonicsBinauralEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createAmbisonicsBinauralEffect(audioSettings, effectSettings, reinterpret_cast<api::IAmbisonicsBinauralEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLAmbisonicsBinauralEffect IPLCALL iplAmbisonicsBinauralEffectRetain(IPLAmbisonicsBinauralEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLAmbisonicsBinauralEffect>(reinterpret_cast<api::IAmbisonicsBinauralEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplAmbisonicsBinauralEffectRelease(IPLAmbisonicsBinauralEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IAmbisonicsBinauralEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAmbisonicsBinauralEffectReset(IPLAmbisonicsBinauralEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IAmbisonicsBinauralEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsBinauralEffectApply(IPLAmbisonicsBinauralEffect effect,
                                                     IPLAmbisonicsBinauralEffectParams* params,
                                                     IPLAudioBuffer* in,
                                                     IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IAmbisonicsBinauralEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplAmbisonicsBinauralEffectGetTailSize(IPLAmbisonicsBinauralEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IAmbisonicsBinauralEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsBinauralEffectGetTail(IPLAmbisonicsBinauralEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IAmbisonicsBinauralEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplAmbisonicsRotationEffectCreate(IPLContext context,
                                           IPLAudioSettings* audioSettings,
                                           IPLAmbisonicsRotationEffectSettings* effectSettings,
                                           IPLAmbisonicsRotationEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createAmbisonicsRotationEffect(audioSettings, effectSettings, reinterpret_cast<api::IAmbisonicsRotationEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLAmbisonicsRotationEffect IPLCALL iplAmbisonicsRotationEffectRetain(IPLAmbisonicsRotationEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLAmbisonicsRotationEffect>(reinterpret_cast<api::IAmbisonicsRotationEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplAmbisonicsRotationEffectRelease(IPLAmbisonicsRotationEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IAmbisonicsRotationEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAmbisonicsRotationEffectReset(IPLAmbisonicsRotationEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IAmbisonicsRotationEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsRotationEffectApply(IPLAmbisonicsRotationEffect effect,
                                                     IPLAmbisonicsRotationEffectParams* params,
                                                     IPLAudioBuffer* in,
                                                     IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IAmbisonicsRotationEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplAmbisonicsRotationEffectGetTailSize(IPLAmbisonicsRotationEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IAmbisonicsRotationEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsRotationEffectGetTail(IPLAmbisonicsRotationEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IAmbisonicsRotationEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplAmbisonicsDecodeEffectCreate(IPLContext context,
                                         IPLAudioSettings* audioSettings,
                                         IPLAmbisonicsDecodeEffectSettings* effectSettings,
                                         IPLAmbisonicsDecodeEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createAmbisonicsDecodeEffect(audioSettings, effectSettings, reinterpret_cast<api::IAmbisonicsDecodeEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLAmbisonicsDecodeEffect IPLCALL iplAmbisonicsDecodeEffectRetain(IPLAmbisonicsDecodeEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLAmbisonicsDecodeEffect>(reinterpret_cast<api::IAmbisonicsDecodeEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplAmbisonicsDecodeEffectRelease(IPLAmbisonicsDecodeEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IAmbisonicsDecodeEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplAmbisonicsDecodeEffectReset(IPLAmbisonicsDecodeEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IAmbisonicsDecodeEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsDecodeEffectApply(IPLAmbisonicsDecodeEffect effect,
                                                   IPLAmbisonicsDecodeEffectParams* params,
                                                   IPLAudioBuffer* in,
                                                   IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IAmbisonicsDecodeEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplAmbisonicsDecodeEffectGetTailSize(IPLAmbisonicsDecodeEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IAmbisonicsDecodeEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplAmbisonicsDecodeEffectGetTail(IPLAmbisonicsDecodeEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IAmbisonicsDecodeEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplDirectEffectCreate(IPLContext context,
                               IPLAudioSettings* audioSettings,
                               IPLDirectEffectSettings* effectSettings,
                               IPLDirectEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createDirectEffect(audioSettings, effectSettings, reinterpret_cast<api::IDirectEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLDirectEffect IPLCALL iplDirectEffectRetain(IPLDirectEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLDirectEffect>(reinterpret_cast<api::IDirectEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplDirectEffectRelease(IPLDirectEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IDirectEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplDirectEffectReset(IPLDirectEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IDirectEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplDirectEffectApply(IPLDirectEffect effect,
                                         IPLDirectEffectParams* params,
                                         IPLAudioBuffer* in,
                                         IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IDirectEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplDirectEffectGetTailSize(IPLDirectEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IDirectEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplDirectEffectGetTail(IPLDirectEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IDirectEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplReflectionEffectCreate(IPLContext context,
                                   IPLAudioSettings* audioSettings,
                                   IPLReflectionEffectSettings* effectSettings,
                                   IPLReflectionEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createReflectionEffect(audioSettings, effectSettings, reinterpret_cast<api::IReflectionEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLReflectionEffect IPLCALL iplReflectionEffectRetain(IPLReflectionEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLReflectionEffect>(reinterpret_cast<api::IReflectionEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplReflectionEffectRelease(IPLReflectionEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IReflectionEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplReflectionEffectReset(IPLReflectionEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IReflectionEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplReflectionEffectApply(IPLReflectionEffect effect,
                                             IPLReflectionEffectParams* params,
                                             IPLAudioBuffer* in,
                                             IPLAudioBuffer* out,
                                             IPLReflectionMixer mixer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IReflectionEffect*>(effect)->apply(params, in, out, reinterpret_cast<api::IReflectionMixer*>(mixer));
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplReflectionEffectGetTailSize(IPLReflectionEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IReflectionEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplReflectionEffectGetTail(IPLReflectionEffect effect, IPLAudioBuffer* out, IPLReflectionMixer mixer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IReflectionEffect*>(effect);
        auto _mixer = reinterpret_cast<api::IReflectionMixer*>(mixer);

        return _effect->getTail(out, _mixer);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplReflectionMixerCreate(IPLContext context,
                                  IPLAudioSettings* audioSettings,
                                  IPLReflectionEffectSettings* effectSettings,
                                  IPLReflectionMixer* mixer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createReflectionMixer(audioSettings, effectSettings, reinterpret_cast<api::IReflectionMixer**>(mixer));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLReflectionMixer IPLCALL iplReflectionMixerRetain(IPLReflectionMixer mixer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!mixer)
            return nullptr;

        return reinterpret_cast<IPLReflectionMixer>(reinterpret_cast<api::IReflectionMixer*>(mixer)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplReflectionMixerRelease(IPLReflectionMixer* mixer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!mixer || !*mixer)
            return;

        reinterpret_cast<api::IReflectionMixer*>(*mixer)->release();

        *mixer = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplReflectionMixerReset(IPLReflectionMixer mixer)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!mixer)
            return;

        reinterpret_cast<api::IReflectionMixer*>(mixer)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplReflectionMixerApply(IPLReflectionMixer mixer,
                                            IPLReflectionEffectParams* params,
                                            IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!mixer)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IReflectionMixer*>(mixer)->apply(params, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplPathEffectCreate(IPLContext context,
                             IPLAudioSettings* audioSettings,
                             IPLPathEffectSettings* effectSettings,
                             IPLPathEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createPathEffect(audioSettings, effectSettings, reinterpret_cast<api::IPathEffect**>(effect));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLPathEffect IPLCALL iplPathEffectRetain(IPLPathEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return nullptr;

        return reinterpret_cast<IPLPathEffect>(reinterpret_cast<api::IPathEffect*>(effect)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplPathEffectRelease(IPLPathEffect* effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect || !*effect)
            return;

        reinterpret_cast<api::IPathEffect*>(*effect)->release();

        *effect = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplPathEffectReset(IPLPathEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return;

        reinterpret_cast<api::IPathEffect*>(effect)->reset();
    }
    catch (...)
    {
        return;
    }
}

IPLAudioEffectState IPLCALL iplPathEffectApply(IPLPathEffect effect,
                                       IPLPathEffectParams* params,
                                       IPLAudioBuffer* in,
                                       IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        return reinterpret_cast<api::IPathEffect*>(effect)->apply(params, in, out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLint32 IPLCALL iplPathEffectGetTailSize(IPLPathEffect effect)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return 0;

        auto _effect = reinterpret_cast<api::IPathEffect*>(effect);

        return _effect->getTailSize();
    }
    catch (...)
    {
        return 0;
    }
}

IPLAudioEffectState IPLCALL iplPathEffectGetTail(IPLPathEffect effect, IPLAudioBuffer* out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!effect)
            return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;

        auto _effect = reinterpret_cast<api::IPathEffect*>(effect);

        return _effect->getTail(out);
    }
    catch (...)
    {
        return IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
    }
}

IPLerror IPLCALL iplProbeArrayCreate(IPLContext context,
                             IPLProbeArray* probeArray)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createProbeArray(reinterpret_cast<api::IProbeArray**>(probeArray));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLProbeArray IPLCALL iplProbeArrayRetain(IPLProbeArray probeArray)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeArray)
            return nullptr;

        return reinterpret_cast<IPLProbeArray>(reinterpret_cast<api::IProbeArray*>(probeArray)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplProbeArrayRelease(IPLProbeArray* probeArray)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeArray || !*probeArray)
            return;

        reinterpret_cast<api::IProbeArray*>(*probeArray)->release();

        *probeArray = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplProbeArrayGenerateProbes(IPLProbeArray probeArray, IPLScene scene, IPLProbeGenerationParams* params)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeArray)
            return;

        reinterpret_cast<api::IProbeArray*>(probeArray)->generateProbes(reinterpret_cast<api::IScene*>(scene), params);
    }
    catch (...)
    {
        return;
    }
}

IPLint32 IPLCALL iplProbeArrayGetNumProbes(IPLProbeArray probeArray)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeArray)
            return 0;

        return reinterpret_cast<api::IProbeArray*>(probeArray)->getNumProbes();
    }
    catch (...)
    {
        return 0;
    }
}

IPLSphere IPLCALL iplProbeArrayGetProbe(IPLProbeArray probeArray,
                                IPLint32 index)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeArray)
            return IPLSphere{};

        return reinterpret_cast<api::IProbeArray*>(probeArray)->getProbe(index);
    }
    catch (...)
    {
        return IPLSphere{};
    }
}

IPLerror IPLCALL iplProbeBatchCreate(IPLContext context,
                             IPLProbeBatch* probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createProbeBatch(reinterpret_cast<api::IProbeBatch**>(probeBatch));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLProbeBatch IPLCALL iplProbeBatchRetain(IPLProbeBatch probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return nullptr;

        return reinterpret_cast<IPLProbeBatch>(reinterpret_cast<api::IProbeBatch*>(probeBatch)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplProbeBatchRelease(IPLProbeBatch* probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch || !*probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(*probeBatch)->release();

        *probeBatch = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplProbeBatchLoad(IPLContext context,
                           IPLSerializedObject serializedObject,
                           IPLProbeBatch* probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->loadProbeBatch(reinterpret_cast<api::ISerializedObject*>(serializedObject), reinterpret_cast<api::IProbeBatch**>(probeBatch));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

void IPLCALL iplProbeBatchSave(IPLProbeBatch probeBatch,
                       IPLSerializedObject serializedObject)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->save(reinterpret_cast<api::ISerializedObject*>(serializedObject));
    }
    catch (...)
    {
        return;
    }
}

IPLint32 IPLCALL iplProbeBatchGetNumProbes(IPLProbeBatch probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return 0;

        return reinterpret_cast<api::IProbeBatch*>(probeBatch)->getNumProbes();
    }
    catch (...)
    {
        return 0;
    }
}

void IPLCALL iplProbeBatchAddProbe(IPLProbeBatch probeBatch,
                           IPLSphere probe)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->addProbe(probe);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplProbeBatchAddProbeArray(IPLProbeBatch probeBatch,
                                IPLProbeArray probeArray)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->addProbeArray(reinterpret_cast<api::IProbeArray*>(probeArray));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplProbeBatchRemoveProbe(IPLProbeBatch probeBatch,
                              IPLint32 index)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->removeProbe(index);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplProbeBatchCommit(IPLProbeBatch probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->commit();
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplProbeBatchRemoveData(IPLProbeBatch probeBatch,
                             IPLBakedDataIdentifier* identifier)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->removeData(identifier);
    }
    catch (...)
    {
        return;
    }
}

IPLsize IPLCALL iplProbeBatchGetDataSize(IPLProbeBatch probeBatch,
                                  IPLBakedDataIdentifier* identifier)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return 0;

        return reinterpret_cast<api::IProbeBatch*>(probeBatch)->getDataSize(identifier);
    }
    catch (...)
    {
        return 0;
    }
}

void IPLCALL iplProbeBatchGetEnergyField(IPLProbeBatch probeBatch, IPLBakedDataIdentifier* identifier, IPLint32 probeIndex, IPLEnergyField energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->getEnergyField(identifier, probeIndex, reinterpret_cast<api::IEnergyField*>(energyField));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplProbeBatchGetReverb(IPLProbeBatch probeBatch, IPLBakedDataIdentifier* identifier, IPLint32 probeIndex, IPLfloat32* reverbTimes)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!probeBatch)
            return;

        reinterpret_cast<api::IProbeBatch*>(probeBatch)->getReverb(identifier, probeIndex, reverbTimes);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplReflectionsBakerBake(IPLContext context,
                             IPLReflectionsBakeParams* params,
                             IPLProgressCallback progressCallback,
                             void* userData)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->bakeReflections(params, progressCallback, userData);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplReflectionsBakerCancelBake(IPLContext context)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->cancelBakeReflections();
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplPathBakerBake(IPLContext context,
                      IPLPathBakeParams* params,
                      IPLProgressCallback progressCallback,
                      void* userData)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->bakePaths(params, progressCallback, userData);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplPathBakerCancelBake(IPLContext context)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->cancelBakePaths();
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplSimulatorCreate(IPLContext context,
                            IPLSimulationSettings* settings,
                            IPLSimulator* simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createSimulator(settings, reinterpret_cast<api::ISimulator**>(simulator));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLSimulator IPLCALL iplSimulatorRetain(IPLSimulator simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return nullptr;

        return reinterpret_cast<IPLSimulator>(reinterpret_cast<api::ISimulator*>(simulator)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplSimulatorRelease(IPLSimulator* simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator || !*simulator)
            return;

        reinterpret_cast<api::ISimulator*>(*simulator)->release();

        *simulator = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorSetScene(IPLSimulator simulator,
                          IPLScene scene)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->setScene(reinterpret_cast<api::IScene*>(scene));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorAddProbeBatch(IPLSimulator simulator, IPLProbeBatch probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->addProbeBatch(reinterpret_cast<api::IProbeBatch*>(probeBatch));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorRemoveProbeBatch(IPLSimulator simulator, IPLProbeBatch probeBatch)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->removeProbeBatch(reinterpret_cast<api::IProbeBatch*>(probeBatch));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorSetSharedInputs(IPLSimulator simulator,
                                 IPLSimulationFlags flags,
                                 IPLSimulationSharedInputs* sharedInputs)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->setSharedInputs(flags, sharedInputs);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorCommit(IPLSimulator simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->commit();
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorRunDirect(IPLSimulator simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->runDirect();
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorRunReflections(IPLSimulator simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->runReflections();
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSimulatorRunPathing(IPLSimulator simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return;

        reinterpret_cast<api::ISimulator*>(simulator)->runPathing();
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplSourceCreate(IPLSimulator simulator,
                         IPLSourceSettings* settings,
                         IPLSource* source)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!simulator)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::ISimulator*>(simulator)->createSource(settings, reinterpret_cast<api::ISource**>(source));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLSource IPLCALL iplSourceRetain(IPLSource source)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!source)
            return nullptr;

        return reinterpret_cast<IPLSource>(reinterpret_cast<api::ISource*>(source)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplSourceRelease(IPLSource* source)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!source || !*source)
            return;

        reinterpret_cast<api::ISource*>(*source)->release();

        *source = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSourceAdd(IPLSource source, IPLSimulator simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!source)
            return;

        reinterpret_cast<api::ISource*>(source)->add(reinterpret_cast<api::ISimulator*>(simulator));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSourceRemove(IPLSource source, IPLSimulator simulator)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!source)
            return;

        reinterpret_cast<api::ISource*>(source)->remove(reinterpret_cast<api::ISimulator*>(simulator));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSourceSetInputs(IPLSource source,
                        IPLSimulationFlags flags,
                        IPLSimulationInputs* inputs)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!source)
            return;

        reinterpret_cast<api::ISource*>(source)->setInputs(flags, inputs);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplSourceGetOutputs(IPLSource source,
                         IPLSimulationFlags flags,
                         IPLSimulationOutputs* outputs)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!source)
            return;

        reinterpret_cast<api::ISource*>(source)->getOutputs(flags, outputs);
    }
    catch (...)
    {
        return;
    }
}

IPLfloat32 IPLCALL iplDistanceAttenuationCalculate(IPLContext context,
                                           IPLVector3 source,
                                           IPLVector3 listener,
                                           IPLDistanceAttenuationModel* model)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return 1.0f;

        return reinterpret_cast<api::IContext*>(context)->calculateDistanceAttenuation(source, listener, model);
    }
    catch (...)
    {
        return 0;
    }
}

void IPLCALL iplAirAbsorptionCalculate(IPLContext context,
                               IPLVector3 source,
                               IPLVector3 listener,
                               IPLAirAbsorptionModel* model,
                               IPLfloat32* airAbsorption)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return;

        reinterpret_cast<api::IContext*>(context)->calculateAirAbsorption(source, listener, model, airAbsorption);
    }
    catch (...)
    {
        return;
    }
}

IPLfloat32 IPLCALL iplDirectivityCalculate(IPLContext context,
                                   IPLCoordinateSpace3 source,
                                   IPLVector3 listener,
                                   IPLDirectivity* model)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return 1.0f;

        return reinterpret_cast<api::IContext*>(context)->calculateDirectivity(source, listener, model);
    }
    catch (...)
    {
        return 0;
    }
}

IPLerror IPLCALL iplEnergyFieldCreate(IPLContext context, IPLEnergyFieldSettings* settings, IPLEnergyField* energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createEnergyField(settings, reinterpret_cast<api::IEnergyField**>(energyField));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLEnergyField IPLCALL iplEnergyFieldRetain(IPLEnergyField energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField)
            return nullptr;

        return reinterpret_cast<IPLEnergyField>(reinterpret_cast<api::IEnergyField*>(energyField)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplEnergyFieldRelease(IPLEnergyField* energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField || !*energyField)
            return;

        reinterpret_cast<api::IEnergyField*>(*energyField)->release();
        *energyField = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLint32 IPLCALL iplEnergyFieldGetNumChannels(IPLEnergyField energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField)
            return 0;

        return reinterpret_cast<api::IEnergyField*>(energyField)->getNumChannels();
    }
    catch (...)
    {
        return 0;
    }
}

IPLint32 IPLCALL iplEnergyFieldGetNumBins(IPLEnergyField energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField)
            return 0;

        return reinterpret_cast<api::IEnergyField*>(energyField)->getNumBins();
    }
    catch (...)
    {
        return 0;
    }
}

IPLfloat32* IPLCALL iplEnergyFieldGetData(IPLEnergyField energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField)
            return nullptr;

        return reinterpret_cast<api::IEnergyField*>(energyField)->getData();
    }
    catch (...)
    {
        return nullptr;
    }
}

IPLfloat32* IPLCALL iplEnergyFieldGetChannel(IPLEnergyField energyField, IPLint32 channelIndex)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField)
            return nullptr;

        return reinterpret_cast<api::IEnergyField*>(energyField)->getChannel(channelIndex);
    }
    catch (...)
    {
        return nullptr;
    }
}

IPLfloat32* IPLCALL iplEnergyFieldGetBand(IPLEnergyField energyField, IPLint32 channelIndex, IPLint32 bandIndex)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField)
            return nullptr;

        return reinterpret_cast<api::IEnergyField*>(energyField)->getBand(channelIndex, bandIndex);
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplEnergyFieldReset(IPLEnergyField energyField)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!energyField)
            return;

        reinterpret_cast<api::IEnergyField*>(energyField)->reset();
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplEnergyFieldCopy(IPLEnergyField src, IPLEnergyField dst)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!src || !dst)
            return;

        reinterpret_cast<api::IEnergyField*>(dst)->copy(reinterpret_cast<api::IEnergyField*>(src));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplEnergyFieldSwap(IPLEnergyField a, IPLEnergyField b)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!a || !b)
            return;

        reinterpret_cast<api::IEnergyField*>(b)->swap(reinterpret_cast<api::IEnergyField*>(a));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplEnergyFieldAdd(IPLEnergyField in1, IPLEnergyField in2, IPLEnergyField out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!in1 || !in2 || !out)
            return;

        reinterpret_cast<api::IEnergyField*>(out)->add(reinterpret_cast<api::IEnergyField*>(in1), reinterpret_cast<api::IEnergyField*>(in2));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplEnergyFieldScale(IPLEnergyField in, IPLfloat32 scalar, IPLEnergyField out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!in || !out)
            return;

        reinterpret_cast<api::IEnergyField*>(out)->scale(reinterpret_cast<api::IEnergyField*>(in), scalar);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplEnergyFieldScaleAccum(IPLEnergyField in, IPLfloat32 scalar, IPLEnergyField out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!in || !out)
            return;

        reinterpret_cast<api::IEnergyField*>(out)->scaleAccum(reinterpret_cast<api::IEnergyField*>(in), scalar);
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplImpulseResponseCreate(IPLContext context, IPLImpulseResponseSettings* settings, IPLImpulseResponse* impulseResponse)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createImpulseResponse(settings, reinterpret_cast<api::IImpulseResponse**>(impulseResponse));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLImpulseResponse IPLCALL iplImpulseResponseRetain(IPLImpulseResponse impulseResponse)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!impulseResponse)
            return nullptr;

        return reinterpret_cast<IPLImpulseResponse>(reinterpret_cast<api::IImpulseResponse*>(impulseResponse)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplImpulseResponseRelease(IPLImpulseResponse* impulseResponse)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!impulseResponse || !*impulseResponse)
            return;

        reinterpret_cast<api::IImpulseResponse*>(*impulseResponse)->release();
        *impulseResponse = nullptr;
    }
    catch (...)
    {
        return;
    }
}

IPLint32 IPLCALL iplImpulseResponseGetNumChannels(IPLImpulseResponse impulseResponse)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!impulseResponse)
            return 0;

        return reinterpret_cast<api::IImpulseResponse*>(impulseResponse)->getNumChannels();
    }
    catch (...)
    {
        return 0;
    }
}

IPLint32 IPLCALL iplImpulseResponseGetNumSamples(IPLImpulseResponse impulseResponse)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!impulseResponse)
            return 0;

        return reinterpret_cast<api::IImpulseResponse*>(impulseResponse)->getNumSamples();
    }
    catch (...)
    {
        return 0;
    }
}

IPLfloat32* IPLCALL iplImpulseResponseGetData(IPLImpulseResponse impulseResponse)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!impulseResponse)
            return nullptr;

        return reinterpret_cast<api::IImpulseResponse*>(impulseResponse)->getData();
    }
    catch (...)
    {
        return nullptr;
    }
}

IPLfloat32* IPLCALL iplImpulseResponseGetChannel(IPLImpulseResponse impulseResponse, int channelIndex)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!impulseResponse)
            return nullptr;

        return reinterpret_cast<api::IImpulseResponse*>(impulseResponse)->getChannel(channelIndex);
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplImpulseResponseReset(IPLImpulseResponse impulseResponse)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!impulseResponse)
            return;

        reinterpret_cast<api::IImpulseResponse*>(impulseResponse)->reset();
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplImpulseResponseCopy(IPLImpulseResponse src, IPLImpulseResponse dst)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!src || !dst)
            return;

        reinterpret_cast<api::IImpulseResponse*>(dst)->copy(reinterpret_cast<api::IImpulseResponse*>(src));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplImpulseResponseSwap(IPLImpulseResponse ir1, IPLImpulseResponse ir2)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!ir1 || !ir2)
            return;

        reinterpret_cast<api::IImpulseResponse*>(ir2)->swap(reinterpret_cast<api::IImpulseResponse*>(ir1));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplImpulseResponseAdd(IPLImpulseResponse in1, IPLImpulseResponse in2, IPLImpulseResponse out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!in1 || !in2 || !out)
            return;

        reinterpret_cast<api::IImpulseResponse*>(out)->add(reinterpret_cast<api::IImpulseResponse*>(in1), reinterpret_cast<api::IImpulseResponse*>(in2));
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplImpulseResponseScale(IPLImpulseResponse in, IPLfloat32 scalar, IPLImpulseResponse out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!in || !out)
            return;

        reinterpret_cast<api::IImpulseResponse*>(out)->scale(reinterpret_cast<api::IImpulseResponse*>(in), scalar);
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplImpulseResponseScaleAccum(IPLImpulseResponse in, IPLfloat32 scalar, IPLImpulseResponse out)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!in || !out)
            return;

        reinterpret_cast<api::IImpulseResponse*>(out)->scaleAccum(reinterpret_cast<api::IImpulseResponse*>(in), scalar);
    }
    catch (...)
    {
        return;
    }
}

IPLerror IPLCALL iplReconstructorCreate(IPLContext context, IPLReconstructorSettings* settings, IPLReconstructor* reconstructor)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!context)
            return IPL_STATUS_FAILURE;

        return reinterpret_cast<api::IContext*>(context)->createReconstructor(settings, reinterpret_cast<api::IReconstructor**>(reconstructor));
    }
    catch (...)
    {
        return IPL_STATUS_FAILURE;
    }
}

IPLReconstructor IPLCALL iplReconstructorRetain(IPLReconstructor reconstructor)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!reconstructor)
            return nullptr;

        return reinterpret_cast<IPLReconstructor>(reinterpret_cast<api::IReconstructor*>(reconstructor)->retain());
    }
    catch (...)
    {
        return nullptr;
    }
}

void IPLCALL iplReconstructorRelease(IPLReconstructor* reconstructor)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!reconstructor || !*reconstructor)
            return;

        reinterpret_cast<api::IReconstructor*>(*reconstructor)->release();
        *reconstructor = nullptr;
    }
    catch (...)
    {
        return;
    }
}

void IPLCALL iplReconstructorReconstruct(IPLReconstructor reconstructor, IPLint32 numInputs, IPLReconstructorInputs* inputs, IPLReconstructorSharedInputs* sharedInputs, IPLReconstructorOutputs* outputs)
{
    // [godot] C API boundary hardening: the engine builds without exceptions,
    // so nothing may unwind past this function. See patches/0001.
    try
    {
        if (!reconstructor)
            return;

        reinterpret_cast<api::IReconstructor*>(reconstructor)->reconstruct(numInputs, inputs, sharedInputs, outputs);
    }
    catch (...)
    {
        return;
    }
}
#endif

#endif
