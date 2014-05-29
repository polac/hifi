//
//  Avatar.cpp
//  interface/src/avatar
//
//  Created by Philip Rosedale on 9/11/12.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/vector_angle.hpp>

#include <NodeList.h>
#include <PacketHeaders.h>
#include <SharedUtil.h>

#include <GeometryUtil.h>

#include "Application.h"
#include "Avatar.h"
#include "Hand.h"
#include "Head.h"
#include "Menu.h"
#include "Physics.h"
#include "world.h"
#include "devices/OculusManager.h"
#include "renderer/TextureCache.h"
#include "ui/TextRenderer.h"

using namespace std;

const glm::vec3 DEFAULT_UP_DIRECTION(0.0f, 1.0f, 0.0f);
const int   NUM_BODY_CONE_SIDES = 9;
const float CHAT_MESSAGE_SCALE = 0.0015f;
const float CHAT_MESSAGE_HEIGHT = 0.1f;
const float DISPLAYNAME_FADE_TIME = 0.5f;
const float DISPLAYNAME_FADE_FACTOR = pow(0.01f, 1.0f / DISPLAYNAME_FADE_TIME);
const float DISPLAYNAME_ALPHA = 0.95f;
const float DISPLAYNAME_BACKGROUND_ALPHA = 0.4f;

Avatar::Avatar() :
    AvatarData(),
    _skeletonModel(this),
    _bodyYawDelta(0.0f),
    _velocity(0.0f, 0.0f, 0.0f),
    _leanScale(0.5f),
    _scale(1.0f),
    _worldUpDirection(DEFAULT_UP_DIRECTION),
    _mouseRayOrigin(0.0f, 0.0f, 0.0f),
    _mouseRayDirection(0.0f, 0.0f, 0.0f),
    _moving(false),
    _collisionGroups(0),
    _initialized(false),
    _shouldRenderBillboard(true)
{
    // we may have been created in the network thread, but we live in the main thread
    moveToThread(Application::getInstance()->thread());
    
    // give the pointer to our head to inherited _headData variable from AvatarData
    _headData = static_cast<HeadData*>(new Head(this));
    _handData = static_cast<HandData*>(new Hand(this));
}

Avatar::~Avatar() {
}

const float BILLBOARD_LOD_DISTANCE = 40.0f;

void Avatar::init() {
    getHead()->init();
    _skeletonModel.init();
    _initialized = true;
    _shouldRenderBillboard = (getLODDistance() >= BILLBOARD_LOD_DISTANCE);
}

glm::vec3 Avatar::getChestPosition() const {
    // for now, let's just assume that the "chest" is halfway between the root and the neck
    glm::vec3 neckPosition;
    return _skeletonModel.getNeckPosition(neckPosition) ? (_position + neckPosition) * 0.5f : _position;
}

glm::quat Avatar::getWorldAlignedOrientation () const {
    return computeRotationFromBodyToWorldUp() * getOrientation();
}

float Avatar::getLODDistance() const {
    return Menu::getInstance()->getAvatarLODDistanceMultiplier() *
        glm::distance(Application::getInstance()->getCamera()->getPosition(), _position) / _scale;
}

void Avatar::simulate(float deltaTime) {
    if (_scale != _targetScale) {
        setScale(_targetScale);
    }

    // update the billboard render flag
    const float BILLBOARD_HYSTERESIS_PROPORTION = 0.1f;
    if (_shouldRenderBillboard) {
        if (getLODDistance() < BILLBOARD_LOD_DISTANCE * (1.0f - BILLBOARD_HYSTERESIS_PROPORTION)) {
            _shouldRenderBillboard = false;
        }
    } else if (getLODDistance() > BILLBOARD_LOD_DISTANCE * (1.0f + BILLBOARD_HYSTERESIS_PROPORTION)) {
        _shouldRenderBillboard = true;
    }

    // simple frustum check
    float boundingRadius = getBillboardSize();
    bool inViewFrustum = Application::getInstance()->getViewFrustum()->sphereInFrustum(_position, boundingRadius) !=
        ViewFrustum::OUTSIDE;

    getHand()->simulate(deltaTime, false);
    _skeletonModel.setLODDistance(getLODDistance());
    
    if (!_shouldRenderBillboard && inViewFrustum) {
        if (_hasNewJointRotations) {
            for (int i = 0; i < _jointData.size(); i++) {
                const JointData& data = _jointData.at(i);
                _skeletonModel.setJointState(i, data.valid, data.rotation);
            }
            _skeletonModel.simulate(deltaTime);
        }
        _skeletonModel.simulate(deltaTime, _hasNewJointRotations);
        simulateAttachments(deltaTime);
        _hasNewJointRotations = false;

        glm::vec3 headPosition = _position;
        _skeletonModel.getHeadPosition(headPosition);
        Head* head = getHead();
        head->setPosition(headPosition);
        head->setScale(_scale);
        head->simulate(deltaTime, false, _shouldRenderBillboard);
    }
    
    // update position by velocity, and subtract the change added earlier for gravity
    _position += _velocity * deltaTime;
    
    // update animation for display name fade in/out
    if ( _displayNameTargetAlpha != _displayNameAlpha) {
        // the alpha function is 
        // Fade out => alpha(t) = factor ^ t => alpha(t+dt) = alpha(t) * factor^(dt)
        // Fade in  => alpha(t) = 1 - factor^t => alpha(t+dt) = 1-(1-alpha(t))*coef^(dt)
        // factor^(dt) = coef
        float coef = pow(DISPLAYNAME_FADE_FACTOR, deltaTime);
        if (_displayNameTargetAlpha < _displayNameAlpha) {
            // Fading out
            _displayNameAlpha *= coef;
        } else {
            // Fading in
            _displayNameAlpha = 1 - (1 - _displayNameAlpha) * coef;
        }
        _displayNameAlpha = abs(_displayNameAlpha - _displayNameTargetAlpha) < 0.01f ? _displayNameTargetAlpha : _displayNameAlpha;
    }
}

void Avatar::setMouseRay(const glm::vec3 &origin, const glm::vec3 &direction) {
    _mouseRayOrigin = origin;
    _mouseRayDirection = direction;
}

enum TextRendererType {
    CHAT, 
    DISPLAYNAME
};

static TextRenderer* textRenderer(TextRendererType type) {
    static TextRenderer* chatRenderer = new TextRenderer(SANS_FONT_FAMILY, 24, -1, false, TextRenderer::SHADOW_EFFECT);
    static TextRenderer* displayNameRenderer = new TextRenderer(SANS_FONT_FAMILY, 12, -1, false, TextRenderer::NO_EFFECT);

    switch(type) {
    case CHAT:
        return chatRenderer;
    case DISPLAYNAME:
        return displayNameRenderer;
    }

    return displayNameRenderer;
}

void Avatar::render(const glm::vec3& cameraPosition, RenderMode renderMode) {
    // simple frustum check
    float boundingRadius = getBillboardSize();
    ViewFrustum* frustum = (renderMode == Avatar::SHADOW_RENDER_MODE) ?
        Application::getInstance()->getShadowViewFrustum() : Application::getInstance()->getViewFrustum();
    if (frustum->sphereInFrustum(_position, boundingRadius) == ViewFrustum::OUTSIDE) {
        return;
    }

    glm::vec3 toTarget = cameraPosition - Application::getInstance()->getAvatar()->getPosition();
    float distanceToTarget = glm::length(toTarget);
   
    {
        // glow when moving far away
        const float GLOW_DISTANCE = 20.0f;
        const float GLOW_MAX_LOUDNESS = 2500.0f;
        const float MAX_GLOW = 0.5f;
        
        float GLOW_FROM_AVERAGE_LOUDNESS = ((this == Application::getInstance()->getAvatar())
                                            ? 0.0f
                                            : MAX_GLOW * getHeadData()->getAudioLoudness() / GLOW_MAX_LOUDNESS);
        if (!Menu::getInstance()->isOptionChecked(MenuOption::GlowWhenSpeaking)) {
            GLOW_FROM_AVERAGE_LOUDNESS = 0.0f;
        }
            
        float glowLevel = _moving && distanceToTarget > GLOW_DISTANCE && renderMode == NORMAL_RENDER_MODE
                      ? 1.0f
                      : GLOW_FROM_AVERAGE_LOUDNESS;

        // render body
        if (Menu::getInstance()->isOptionChecked(MenuOption::Avatars)) {
            renderBody(renderMode, glowLevel);
        }
        if (renderMode != SHADOW_RENDER_MODE) {
            bool renderSkeleton = Menu::getInstance()->isOptionChecked(MenuOption::RenderSkeletonCollisionShapes);
            bool renderHead = Menu::getInstance()->isOptionChecked(MenuOption::RenderHeadCollisionShapes);
            bool renderBounding = Menu::getInstance()->isOptionChecked(MenuOption::RenderBoundingCollisionShapes);
            if (renderSkeleton || renderHead || renderBounding) {
                updateShapePositions();
            }

            if (renderSkeleton) {
                _skeletonModel.renderJointCollisionShapes(0.7f);
            }

            if (renderHead && shouldRenderHead(cameraPosition, renderMode)) {
                getHead()->getFaceModel().renderJointCollisionShapes(0.7f);
            }
            if (renderBounding && shouldRenderHead(cameraPosition, renderMode)) {
                getHead()->getFaceModel().renderBoundingCollisionShapes(0.7f);
                _skeletonModel.renderBoundingCollisionShapes(0.7f);
            }

            // If this is the avatar being looked at, render a little ball above their head
            if (_isLookAtTarget) {
                const float LOOK_AT_INDICATOR_RADIUS = 0.03f;
                const float LOOK_AT_INDICATOR_OFFSET = 0.22f;
                const float LOOK_AT_INDICATOR_COLOR[] = { 0.8f, 0.0f, 0.0f, 0.75f };
                glPushMatrix();
                glColor4fv(LOOK_AT_INDICATOR_COLOR);
                if (_displayName.isEmpty() || _displayNameAlpha == 0.0f) {
                    glTranslatef(_position.x, getDisplayNamePosition().y, _position.z);
                } else {
                    glTranslatef(_position.x, getDisplayNamePosition().y + LOOK_AT_INDICATOR_OFFSET, _position.z);
                }
                glutSolidSphere(LOOK_AT_INDICATOR_RADIUS, 15, 15);
                glPopMatrix();
            }
        }

        // quick check before falling into the code below:
        // (a 10 degree breadth of an almost 2 meter avatar kicks in at about 12m)
        const float MIN_VOICE_SPHERE_DISTANCE = 12.0f;
        if (distanceToTarget > MIN_VOICE_SPHERE_DISTANCE) {
            // render voice intensity sphere for avatars that are farther away
            const float MAX_SPHERE_ANGLE = 10.0f * RADIANS_PER_DEGREE;
            const float MIN_SPHERE_ANGLE = 1.0f * RADIANS_PER_DEGREE;
            const float MIN_SPHERE_SIZE = 0.01f;
            const float SPHERE_LOUDNESS_SCALING = 0.0005f;
            const float SPHERE_COLOR[] = { 0.5f, 0.8f, 0.8f };
            float height = getSkeletonHeight();
            glm::vec3 delta = height * (getHead()->getCameraOrientation() * IDENTITY_UP) / 2.0f;
            float angle = abs(angleBetween(toTarget + delta, toTarget - delta));
            float sphereRadius = getHead()->getAverageLoudness() * SPHERE_LOUDNESS_SCALING;
            
            if (renderMode == NORMAL_RENDER_MODE && (sphereRadius > MIN_SPHERE_SIZE) &&
                    (angle < MAX_SPHERE_ANGLE) && (angle > MIN_SPHERE_ANGLE)) {
                glColor4f(SPHERE_COLOR[0], SPHERE_COLOR[1], SPHERE_COLOR[2], 1.0f - angle / MAX_SPHERE_ANGLE);
                glPushMatrix();
                glTranslatef(_position.x, _position.y, _position.z);
                glScalef(height, height, height);
                glutSolidSphere(sphereRadius, 15, 15);
                glPopMatrix();
            }
        }
    }

    const float DISPLAYNAME_DISTANCE = 10.0f;
    setShowDisplayName(renderMode == NORMAL_RENDER_MODE && distanceToTarget < DISPLAYNAME_DISTANCE);
    if (renderMode != NORMAL_RENDER_MODE || (isMyAvatar() &&
            Application::getInstance()->getCamera()->getMode() == CAMERA_MODE_FIRST_PERSON)) {
        return;
    }
    renderDisplayName();
    
    if (!_chatMessage.empty()) {
        int width = 0;
        int lastWidth = 0;
        for (string::iterator it = _chatMessage.begin(); it != _chatMessage.end(); it++) {
            width += (lastWidth = textRenderer(CHAT)->computeWidth(*it));
        }
        glPushMatrix();
        
        glm::vec3 chatPosition = getHead()->getEyePosition() + getBodyUpDirection() * CHAT_MESSAGE_HEIGHT * _scale;
        glTranslatef(chatPosition.x, chatPosition.y, chatPosition.z);
        glm::quat chatRotation = Application::getInstance()->getCamera()->getRotation();
        glm::vec3 chatAxis = glm::axis(chatRotation);
        glRotatef(glm::degrees(glm::angle(chatRotation)), chatAxis.x, chatAxis.y, chatAxis.z);
        
        glColor3f(0.0f, 0.8f, 0.0f);
        glRotatef(180.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(180.0f, 0.0f, 0.0f, 1.0f);
        glScalef(_scale * CHAT_MESSAGE_SCALE, _scale * CHAT_MESSAGE_SCALE, 1.0f);
        
        glDisable(GL_LIGHTING);
        glDepthMask(false);
        if (_keyState == NO_KEY_DOWN) {
            textRenderer(CHAT)->draw(-width / 2.0f, 0, _chatMessage.c_str());
            
        } else {
            // rather than using substr and allocating a new string, just replace the last
            // character with a null, then restore it
            int lastIndex = _chatMessage.size() - 1;
            char lastChar = _chatMessage[lastIndex];
            _chatMessage[lastIndex] = '\0';
            textRenderer(CHAT)->draw(-width / 2.0f, 0, _chatMessage.c_str());
            _chatMessage[lastIndex] = lastChar;
            glColor3f(0.0f, 1.0f, 0.0f);
            textRenderer(CHAT)->draw(width / 2.0f - lastWidth, 0, _chatMessage.c_str() + lastIndex);
        }
        glEnable(GL_LIGHTING);
        glDepthMask(true);
        
        glPopMatrix();
    }
}

glm::quat Avatar::computeRotationFromBodyToWorldUp(float proportion) const {
    glm::quat orientation = getOrientation();
    glm::vec3 currentUp = orientation * IDENTITY_UP;
    float angle = acosf(glm::clamp(glm::dot(currentUp, _worldUpDirection), -1.0f, 1.0f));
    if (angle < EPSILON) {
        return glm::quat();
    }
    glm::vec3 axis;
    if (angle > 179.99f * RADIANS_PER_DEGREE) { // 180 degree rotation; must use another axis
        axis = orientation * IDENTITY_RIGHT;
    } else {
        axis = glm::normalize(glm::cross(currentUp, _worldUpDirection));
    }
    return glm::angleAxis(angle * proportion, axis);
}

void Avatar::renderBody(RenderMode renderMode, float glowLevel) {
    Model::RenderMode modelRenderMode = (renderMode == SHADOW_RENDER_MODE) ?
                            Model::SHADOW_RENDER_MODE : Model::DEFAULT_RENDER_MODE;
    {
        Glower glower(glowLevel);
        
        if (_shouldRenderBillboard || !(_skeletonModel.isRenderable() && getHead()->getFaceModel().isRenderable())) {
            // render the billboard until both models are loaded
            renderBillboard();
            return;
        }
        
        _skeletonModel.render(1.0f, modelRenderMode, Menu::getInstance()->isOptionChecked(MenuOption::AvatarsReceiveShadows));
        renderAttachments(renderMode);
        getHand()->render(false, modelRenderMode);
    }
    getHead()->render(1.0f, modelRenderMode);
}

bool Avatar::shouldRenderHead(const glm::vec3& cameraPosition, RenderMode renderMode) const {
    return true;
}

void Avatar::simulateAttachments(float deltaTime) {
    for (int i = 0; i < _attachmentModels.size(); i++) {
        const AttachmentData& attachment = _attachmentData.at(i);
        Model* model = _attachmentModels.at(i);
        int jointIndex = getJointIndex(attachment.jointName);
        glm::vec3 jointPosition;
        glm::quat jointRotation;
        if (!isMyAvatar()) {
            model->setLODDistance(getLODDistance());
        }
        if (_skeletonModel.getJointPosition(jointIndex, jointPosition) &&
                _skeletonModel.getJointRotation(jointIndex, jointRotation)) {
            model->setTranslation(jointPosition + jointRotation * attachment.translation * _scale);
            model->setRotation(jointRotation * attachment.rotation);
            model->setScale(_skeletonModel.getScale() * attachment.scale);
            model->simulate(deltaTime);
        }
    }
}

void Avatar::renderAttachments(RenderMode renderMode) {
    Model::RenderMode modelRenderMode = (renderMode == SHADOW_RENDER_MODE) ?
        Model::SHADOW_RENDER_MODE : Model::DEFAULT_RENDER_MODE;
    bool receiveShadows = Menu::getInstance()->isOptionChecked(MenuOption::AvatarsReceiveShadows);
    foreach (Model* model, _attachmentModels) {
        model->render(1.0f, modelRenderMode, receiveShadows);
    }
}

void Avatar::updateJointMappings() {
    // no-op; joint mappings come from skeleton model
}

void Avatar::renderBillboard() {
    if (_billboard.isEmpty()) {
        return;
    }
    if (!_billboardTexture) {
        QImage image = QImage::fromData(_billboard);
        if (image.format() != QImage::Format_ARGB32) {
            image = image.convertToFormat(QImage::Format_ARGB32);
        }
        _billboardTexture.reset(new Texture());
        glBindTexture(GL_TEXTURE_2D, _billboardTexture->getID());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0,
            GL_BGRA, GL_UNSIGNED_BYTE, image.constBits());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    } else {
        glBindTexture(GL_TEXTURE_2D, _billboardTexture->getID());
    }
    
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    
    glPushMatrix();
    glTranslatef(_position.x, _position.y, _position.z);
    
    // rotate about vertical to face the camera
    glm::quat rotation = getOrientation();
    glm::vec3 cameraVector = glm::inverse(rotation) * (Application::getInstance()->getCamera()->getPosition() - _position);
    rotation = rotation * glm::angleAxis(atan2f(-cameraVector.x, -cameraVector.z), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 axis = glm::axis(rotation);
    glRotatef(glm::degrees(glm::angle(rotation)), axis.x, axis.y, axis.z);
    
    // compute the size from the billboard camera parameters and scale
    float size = getBillboardSize();
    glScalef(size, size, size);
    
    glColor3f(1.0f, 1.0f, 1.0f);
    
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    
    glPopMatrix();
    
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    
    glBindTexture(GL_TEXTURE_2D, 0);
}

float Avatar::getBillboardSize() const {
    return _scale * BILLBOARD_DISTANCE * tanf(glm::radians(BILLBOARD_FIELD_OF_VIEW / 2.0f));
}

glm::vec3 Avatar::getDisplayNamePosition() {
    glm::vec3 namePosition;
    if (getSkeletonModel().getNeckPosition(namePosition)) {
        namePosition += getBodyUpDirection() * getHeadHeight() * 1.1f;
    } else {
        const float HEAD_PROPORTION = 0.75f;
        namePosition = _position + getBodyUpDirection() * (getBillboardSize() * HEAD_PROPORTION);
    }
    return namePosition;
}

void Avatar::renderDisplayName() {

    if (_displayName.isEmpty() || _displayNameAlpha == 0.0f) {
        return;
    }
       
    glDisable(GL_LIGHTING);
    
    glPushMatrix();
    glm::vec3 textPosition = getDisplayNamePosition();
    
    glTranslatef(textPosition.x, textPosition.y, textPosition.z); 

    // we need "always facing camera": we must remove the camera rotation from the stack
    glm::quat rotation = Application::getInstance()->getCamera()->getRotation();
    glm::vec3 axis = glm::axis(rotation);
    glRotatef(glm::degrees(glm::angle(rotation)), axis.x, axis.y, axis.z);

    // We need to compute the scale factor such as the text remains with fixed size respect to window coordinates
    // We project a unit vector and check the difference in screen coordinates, to check which is the 
    // correction scale needed
    // save the matrices for later scale correction factor 
    glm::dmat4 modelViewMatrix;
    glm::dmat4 projectionMatrix;
    GLint viewportMatrix[4];
    Application::getInstance()->getModelViewMatrix(&modelViewMatrix);
    Application::getInstance()->getProjectionMatrix(&projectionMatrix);
    glGetIntegerv(GL_VIEWPORT, viewportMatrix);
    GLdouble result0[3], result1[3];

    // The up vector must be relative to the rotation current rotation matrix:
    // we set the identity
    glm::dvec3 testPoint0 = glm::dvec3(textPosition);
    glm::dvec3 testPoint1 = glm::dvec3(textPosition) + glm::dvec3(Application::getInstance()->getCamera()->getRotation() * IDENTITY_UP);
    
    bool success;
    success = gluProject(testPoint0.x, testPoint0.y, testPoint0.z,
        (GLdouble*)&modelViewMatrix, (GLdouble*)&projectionMatrix, viewportMatrix, 
        &result0[0], &result0[1], &result0[2]);
    success = success && 
        gluProject(testPoint1.x, testPoint1.y, testPoint1.z,
        (GLdouble*)&modelViewMatrix, (GLdouble*)&projectionMatrix, viewportMatrix, 
        &result1[0], &result1[1], &result1[2]);

    if (success) {
        double textWindowHeight = abs(result1[1] - result0[1]);
        float scaleFactor = (textWindowHeight > EPSILON) ? 1.0f / textWindowHeight : 1.0f;
        glScalef(scaleFactor, scaleFactor, 1.0);  
        
        glScalef(1.0f, -1.0f, 1.0f);  // TextRenderer::draw paints the text upside down in y axis

        int text_x = -_displayNameBoundingRect.width() / 2;
        int text_y = -_displayNameBoundingRect.height() / 2;

        // draw a gray background
        int left = text_x + _displayNameBoundingRect.x();
        int right = left + _displayNameBoundingRect.width();
        int bottom = text_y + _displayNameBoundingRect.y();
        int top = bottom + _displayNameBoundingRect.height();
        const int border = 8;
        bottom -= border;
        left -= border;
        top += border;
        right += border;

        // We are drawing coplanar textures with depth: need the polygon offset
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);

        glColor4f(0.2f, 0.2f, 0.2f, _displayNameAlpha * DISPLAYNAME_BACKGROUND_ALPHA / DISPLAYNAME_ALPHA);
        renderBevelCornersRect(left, bottom, right - left, top - bottom, 3);
       
        glColor4f(0.93f, 0.93f, 0.93f, _displayNameAlpha);
        QByteArray ba = _displayName.toLocal8Bit();
        const char* text = ba.data();
        
        glDisable(GL_POLYGON_OFFSET_FILL);
        textRenderer(DISPLAYNAME)->draw(text_x, text_y, text); 
     

    }

    glPopMatrix();

    glEnable(GL_LIGHTING);
}

bool Avatar::findRayIntersection(const glm::vec3& origin, const glm::vec3& direction, float& distance) const {
    float minDistance = FLT_MAX;
    float modelDistance;
    if (_skeletonModel.findRayIntersection(origin, direction, modelDistance)) {
        minDistance = qMin(minDistance, modelDistance);
    }
    if (getHead()->getFaceModel().findRayIntersection(origin, direction, modelDistance)) {
        minDistance = qMin(minDistance, modelDistance);
    }
    if (minDistance < FLT_MAX) {
        distance = minDistance;
        return true;
    }
    return false;
}

bool Avatar::findSphereCollisions(const glm::vec3& penetratorCenter, float penetratorRadius,
        CollisionList& collisions, int skeletonSkipIndex) {
    return _skeletonModel.findSphereCollisions(penetratorCenter, penetratorRadius, collisions, skeletonSkipIndex);
    // Temporarily disabling collisions against the head because most of its collision proxies are bad.
    //return getHead()->getFaceModel().findSphereCollisions(penetratorCenter, penetratorRadius, collisions);
}

bool Avatar::findPlaneCollisions(const glm::vec4& plane, CollisionList& collisions) {
    return _skeletonModel.findPlaneCollisions(plane, collisions) ||
        getHead()->getFaceModel().findPlaneCollisions(plane, collisions);
}

void Avatar::updateShapePositions() {
    _skeletonModel.updateShapePositions();
    Model& headModel = getHead()->getFaceModel();
    headModel.updateShapePositions();
    /* KEEP FOR DEBUG: use this in rather than code above to see shapes 
     * in their default positions where the bounding shape is computed.
    _skeletonModel.resetShapePositions();
    Model& headModel = getHead()->getFaceModel();
    headModel.resetShapePositions();
    */
}

bool Avatar::findCollisions(const QVector<const Shape*>& shapes, CollisionList& collisions) {
    // TODO: Andrew to fix: also collide against _skeleton
    //bool collided = _skeletonModel.findCollisions(shapes, collisions);

    Model& headModel = getHead()->getFaceModel();
    //collided = headModel.findCollisions(shapes, collisions) || collided;
    bool collided = headModel.findCollisions(shapes, collisions);
    return collided;
}

bool Avatar::findParticleCollisions(const glm::vec3& particleCenter, float particleRadius, CollisionList& collisions) {
    if (_collisionGroups & COLLISION_GROUP_PARTICLES) {
        return false;
    }
    bool collided = false;
    // first do the hand collisions
    const HandData* handData = getHandData();
    if (handData) {
        for (int i = 0; i < NUM_HANDS; i++) {
            const PalmData* palm = handData->getPalm(i);
            if (palm && palm->hasPaddle()) {
                // create a disk collision proxy where the hand is
                int jointIndex = -1;
                glm::vec3 handPosition;
                if (i == 0) {
                    _skeletonModel.getLeftHandPosition(handPosition);
                    jointIndex = _skeletonModel.getLeftHandJointIndex();
                }
                else {
                    _skeletonModel.getRightHandPosition(handPosition);
                    jointIndex = _skeletonModel.getRightHandJointIndex();
                }

                glm::vec3 fingerAxis = palm->getFingerDirection();
                glm::vec3 diskCenter = handPosition + HAND_PADDLE_OFFSET * fingerAxis;
                glm::vec3 diskNormal = palm->getNormal();
                const float DISK_THICKNESS = 0.08f;

                // collide against the disk
                glm::vec3 penetration;
                if (findSphereDiskPenetration(particleCenter, particleRadius, 
                            diskCenter, HAND_PADDLE_RADIUS, DISK_THICKNESS, diskNormal,
                            penetration)) {
                    CollisionInfo* collision = collisions.getNewCollision();
                    if (collision) {
                        collision->_type = COLLISION_TYPE_PADDLE_HAND;
                        collision->_intData = jointIndex;
                        collision->_penetration = penetration;
                        collision->_addedVelocity = palm->getVelocity();
                        collided = true;
                    } else {
                        // collisions are full, so we might as well bail now
                        return collided;
                    }
                }
            }
        }
    }
    // then collide against the models
    int preNumCollisions = collisions.size();
    if (_skeletonModel.findSphereCollisions(particleCenter, particleRadius, collisions)) {
        // the Model doesn't have velocity info, so we have to set it for each new collision
        int postNumCollisions = collisions.size();
        for (int i = preNumCollisions; i < postNumCollisions; ++i) {
            CollisionInfo* collision = collisions.getCollision(i);
            collision->_penetration /= (float)(TREE_SCALE);
            collision->_addedVelocity = getVelocity();
        }
        collided = true;
    }
    return collided;
}

glm::quat Avatar::getJointRotation(int index) const {
    if (QThread::currentThread() != thread()) {
        return AvatarData::getJointRotation(index);
    }
    glm::quat rotation;
    _skeletonModel.getJointState(index, rotation);
    return rotation;
}

int Avatar::getJointIndex(const QString& name) const {
    if (QThread::currentThread() != thread()) {
        int result;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointIndex", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(int, result), Q_ARG(const QString&, name));
        return result;
    }
    return _skeletonModel.isActive() ? _skeletonModel.getGeometry()->getFBXGeometry().getJointIndex(name) : -1;
}

QStringList Avatar::getJointNames() const {
    if (QThread::currentThread() != thread()) {
        QStringList result;
        QMetaObject::invokeMethod(const_cast<Avatar*>(this), "getJointNames", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(QStringList, result));
        return result;
    }
    return _skeletonModel.isActive() ? _skeletonModel.getGeometry()->getFBXGeometry().getJointNames() : QStringList();
}

void Avatar::setFaceModelURL(const QUrl& faceModelURL) {
    AvatarData::setFaceModelURL(faceModelURL);
    const QUrl DEFAULT_FACE_MODEL_URL = QUrl::fromLocalFile(Application::resourcesPath() + "meshes/defaultAvatar_head.fst");
    getHead()->getFaceModel().setURL(_faceModelURL, DEFAULT_FACE_MODEL_URL, true, !isMyAvatar());
}

void Avatar::setSkeletonModelURL(const QUrl& skeletonModelURL) {
    AvatarData::setSkeletonModelURL(skeletonModelURL);
    const QUrl DEFAULT_SKELETON_MODEL_URL = QUrl::fromLocalFile(Application::resourcesPath() + "meshes/defaultAvatar_body.fst");
    _skeletonModel.setURL(_skeletonModelURL, DEFAULT_SKELETON_MODEL_URL, true, !isMyAvatar());
}

void Avatar::setAttachmentData(const QVector<AttachmentData>& attachmentData) {
    AvatarData::setAttachmentData(attachmentData);
    if (QThread::currentThread() != thread()) {    
        return;
    }
    // make sure we have as many models as attachments
    while (_attachmentModels.size() < attachmentData.size()) {
        Model* model = new Model(this);
        model->init();
        _attachmentModels.append(model);
    }
    while (_attachmentModels.size() > attachmentData.size()) {
        delete _attachmentModels.takeLast();
    }
    
    // update the urls
    for (int i = 0; i < attachmentData.size(); i++) {
        _attachmentModels[i]->setURL(attachmentData.at(i).modelURL);
    }
}

void Avatar::setDisplayName(const QString& displayName) {
    AvatarData::setDisplayName(displayName);
    _displayNameBoundingRect = textRenderer(DISPLAYNAME)->metrics().tightBoundingRect(displayName);
}

void Avatar::setBillboard(const QByteArray& billboard) {
    AvatarData::setBillboard(billboard);
    
    // clear out any existing billboard texture
    _billboardTexture.reset();
}

int Avatar::parseDataAtOffset(const QByteArray& packet, int offset) {
    if (!_initialized) {
        // now that we have data for this Avatar we are go for init
        init();
    }
    
    // change in position implies movement
    glm::vec3 oldPosition = _position;
    
    int bytesRead = AvatarData::parseDataAtOffset(packet, offset);
    
    const float MOVE_DISTANCE_THRESHOLD = 0.001f;
    _moving = glm::distance(oldPosition, _position) > MOVE_DISTANCE_THRESHOLD;
    
    return bytesRead;
}

// render a makeshift cone section that serves as a body part connecting joint spheres
void Avatar::renderJointConnectingCone(glm::vec3 position1, glm::vec3 position2, float radius1, float radius2) {
    
    glBegin(GL_TRIANGLES);
    
    glm::vec3 axis = position2 - position1;
    float length = glm::length(axis);
    
    if (length > 0.0f) {
        
        axis /= length;
        
        glm::vec3 perpSin = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 perpCos = glm::normalize(glm::cross(axis, perpSin));
        perpSin = glm::cross(perpCos, axis);
        
        float anglea = 0.0f;
        float angleb = 0.0f;
        
        for (int i = 0; i < NUM_BODY_CONE_SIDES; i ++) {
            
            // the rectangles that comprise the sides of the cone section are
            // referenced by "a" and "b" in one dimension, and "1", and "2" in the other dimension.
            anglea = angleb;
            angleb = ((float)(i+1) / (float)NUM_BODY_CONE_SIDES) * TWO_PI;
            
            float sa = sinf(anglea);
            float sb = sinf(angleb);
            float ca = cosf(anglea);
            float cb = cosf(angleb);
            
            glm::vec3 p1a = position1 + perpSin * sa * radius1 + perpCos * ca * radius1;
            glm::vec3 p1b = position1 + perpSin * sb * radius1 + perpCos * cb * radius1; 
            glm::vec3 p2a = position2 + perpSin * sa * radius2 + perpCos * ca * radius2;   
            glm::vec3 p2b = position2 + perpSin * sb * radius2 + perpCos * cb * radius2;  
            
            glVertex3f(p1a.x, p1a.y, p1a.z); 
            glVertex3f(p1b.x, p1b.y, p1b.z); 
            glVertex3f(p2a.x, p2a.y, p2a.z); 
            glVertex3f(p1b.x, p1b.y, p1b.z); 
            glVertex3f(p2a.x, p2a.y, p2a.z); 
            glVertex3f(p2b.x, p2b.y, p2b.z); 
        }
    }
    
    glEnd();
}

void Avatar::updateCollisionGroups() {
    _collisionGroups = 0;
    if (Menu::getInstance()->isOptionChecked(MenuOption::CollideWithEnvironment)) {
        _collisionGroups |= COLLISION_GROUP_ENVIRONMENT;
    }
    if (Menu::getInstance()->isOptionChecked(MenuOption::CollideWithAvatars)) {
        _collisionGroups |= COLLISION_GROUP_AVATARS;
    }
    if (Menu::getInstance()->isOptionChecked(MenuOption::CollideWithVoxels)) {
        _collisionGroups |= COLLISION_GROUP_VOXELS;
    }
    if (Menu::getInstance()->isOptionChecked(MenuOption::CollideWithParticles)) {
        _collisionGroups |= COLLISION_GROUP_PARTICLES;
    }
}

void Avatar::setScale(float scale) {
    _scale = scale;

    if (_targetScale * (1.0f - RESCALING_TOLERANCE) < _scale &&
            _scale < _targetScale * (1.0f + RESCALING_TOLERANCE)) {
        _scale = _targetScale;
    }
}

float Avatar::getSkeletonHeight() const {
    Extents extents = _skeletonModel.getBindExtents();
    return extents.maximum.y - extents.minimum.y;
}

float Avatar::getHeadHeight() const {
    Extents extents = getHead()->getFaceModel().getBindExtents();
    if (!extents.isEmpty()) {
        return extents.maximum.y - extents.minimum.y;
    }
    glm::vec3 neckPosition;
    glm::vec3 headPosition;
    if (_skeletonModel.getNeckPosition(neckPosition) && _skeletonModel.getHeadPosition(headPosition)) {
        return glm::distance(neckPosition, headPosition);
    }
    const float DEFAULT_HEAD_HEIGHT = 0.1f;
    return DEFAULT_HEAD_HEIGHT;
}

bool Avatar::collisionWouldMoveAvatar(CollisionInfo& collision) const {
    if (!collision._data || collision._type != COLLISION_TYPE_MODEL) {
        return false;
    }
    Model* model = static_cast<Model*>(collision._data);
    int jointIndex = collision._intData;

    if (model == &(_skeletonModel) && jointIndex != -1) {
        // collision response of skeleton is temporarily disabled
        return false;
        //return _skeletonModel.collisionHitsMoveableJoint(collision);
    }
    if (model == &(getHead()->getFaceModel())) {
        // ATM we always handle COLLISION_TYPE_MODEL against the face.
        return true;
    }
    return false;
}

float Avatar::getBoundingRadius() const {
    // TODO: also use head model when computing the avatar's bounding radius
    return _skeletonModel.getBoundingRadius();
}

float Avatar::getPelvisFloatingHeight() const {
    return -_skeletonModel.getBindExtents().minimum.y;
}

float Avatar::getPelvisToHeadLength() const {
    return glm::distance(_position, getHead()->getPosition());
}

void Avatar::setShowDisplayName(bool showDisplayName) {
    // For myAvatar, the alpha update is not done (called in simulate for other avatars)
    if (Application::getInstance()->getAvatar() == this) {
        if (showDisplayName) {
            _displayNameAlpha = DISPLAYNAME_ALPHA;
        } else {
            _displayNameAlpha = 0.0f;
        }
    } 

    if (showDisplayName) {
        _displayNameTargetAlpha = DISPLAYNAME_ALPHA;
    } else {
        _displayNameTargetAlpha = 0.0f;
    }

}

