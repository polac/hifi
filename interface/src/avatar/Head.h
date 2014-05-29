//
//  Head.h
//  interface/src/avatar
//
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Head_h
#define hifi_Head_h

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <SharedUtil.h>

#include <HeadData.h>

#include <VoxelConstants.h>

#include "FaceModel.h"
#include "InterfaceConfig.h"
#include "world.h"

enum eyeContactTargets {
    LEFT_EYE, 
    RIGHT_EYE, 
    MOUTH
};

const float EYE_EAR_GAP = 0.08f;

class Avatar;
class ProgramObject;

class Head : public HeadData {
public:
    Head(Avatar* owningAvatar);
    
    void init();
    void reset();
    void simulate(float deltaTime, bool isMine, bool billboard = false);
    void render(float alpha, Model::RenderMode mode);
    void setScale(float scale);
    void setPosition(glm::vec3 position) { _position = position; }
    void setAverageLoudness(float averageLoudness) { _averageLoudness = averageLoudness; }
    void setReturnToCenter (bool returnHeadToCenter) { _returnHeadToCenter = returnHeadToCenter; }
    void setRenderLookatVectors(bool onOff) { _renderLookatVectors = onOff; }
    void setLeanSideways(float leanSideways) { _leanSideways = leanSideways; }
    void setLeanForward(float leanForward) { _leanForward = leanForward; }
    
    /// \return orientationBody * orientationBase+Delta
    glm::quat getFinalOrientation() const;

    /// \return orientationBody * orientationBasePitch
    glm::quat getCameraOrientation () const;

    const glm::vec3& getAngularVelocity() const { return _angularVelocity; }
    void setAngularVelocity(glm::vec3 angularVelocity) { _angularVelocity = angularVelocity; }
    
    float getScale() const { return _scale; }
    glm::vec3 getPosition() const { return _position; }
    const glm::vec3& getEyePosition() const { return _eyePosition; }
    const glm::vec3& getSaccade() const { return _saccade; }
    glm::vec3 getRightDirection() const { return getOrientation() * IDENTITY_RIGHT; }
    glm::vec3 getUpDirection() const { return getOrientation() * IDENTITY_UP; }
    glm::vec3 getFrontDirection() const { return getOrientation() * IDENTITY_FRONT; }
    float getLeanSideways() const { return _leanSideways; }
    float getLeanForward() const { return _leanForward; }
    float getFinalLeanSideways() const { return _leanSideways + _deltaLeanSideways; }
    float getFinalLeanForward() const { return _leanForward + _deltaLeanForward; }
    
    glm::quat getEyeRotation(const glm::vec3& eyePosition) const;
    
    const glm::vec3& getRightEyePosition() const { return _rightEyePosition; }
    const glm::vec3& getLeftEyePosition() const { return _leftEyePosition; }
    glm::vec3 getRightEarPosition() const { return _rightEyePosition + (getRightDirection() * EYE_EAR_GAP) + (getFrontDirection() * -EYE_EAR_GAP); }
    glm::vec3 getLeftEarPosition() const { return _leftEyePosition + (getRightDirection() * -EYE_EAR_GAP) + (getFrontDirection() * -EYE_EAR_GAP); }

    FaceModel& getFaceModel() { return _faceModel; }
    const FaceModel& getFaceModel() const { return _faceModel; }
    
    const bool getReturnToCenter() const { return _returnHeadToCenter; } // Do you want head to try to return to center (depends on interface detected)
    float getAverageLoudness() const { return _averageLoudness; }
    glm::vec3 calculateAverageEyePosition() const { return _leftEyePosition + (_rightEyePosition - _leftEyePosition ) * ONE_HALF; }
    
    /// \return the point about which scaling occurs.
    glm::vec3 getScalePivot() const;

    void setDeltaPitch(float pitch) { _deltaPitch = pitch; }
    float getDeltaPitch() const { return _deltaPitch; }

    void setDeltaYaw(float yaw) { _deltaYaw = yaw; }
    float getDeltaYaw() const { return _deltaYaw; }
    
    void setDeltaRoll(float roll) { _deltaRoll = roll; }
    float getDeltaRoll() const { return _deltaRoll; }
    
    virtual float getFinalPitch() const;
    virtual float getFinalYaw() const;
    virtual float getFinalRoll() const;

    void relaxLean(float deltaTime);
    void addLeanDeltas(float sideways, float forward);
    
private:
    // disallow copies of the Head, copy of owning Avatar is disallowed too
    Head(const Head&);
    Head& operator= (const Head&);

    bool _returnHeadToCenter;
    glm::vec3 _position;
    glm::vec3 _rotation;
    glm::vec3 _leftEyePosition;
    glm::vec3 _rightEyePosition;
    glm::vec3 _eyePosition;
    float _scale;
    float _lastLoudness;
    float _audioAttack;
    glm::vec3 _angularVelocity;
    bool _renderLookatVectors;
    glm::vec3 _saccade;
    glm::vec3 _saccadeTarget;
    float _leftEyeBlinkVelocity;
    float _rightEyeBlinkVelocity;
    float _timeWithoutTalking;

    // delta angles for local head rotation (driven by hardware input)
    float _deltaPitch;
    float _deltaYaw;
    float _deltaRoll;

    // delta lean angles for lean perturbations (driven by collisions)
    float _deltaLeanSideways;
    float _deltaLeanForward;

    bool _isCameraMoving;
    FaceModel _faceModel;
    
    // private methods
    void renderLookatVectors(glm::vec3 leftEyePosition, glm::vec3 rightEyePosition, glm::vec3 lookatPosition);

    friend class FaceModel;
};

#endif // hifi_Head_h
