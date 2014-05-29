//
//  Model.h
//  interface/src/renderer
//
//  Created by Andrzej Kapolka on 10/18/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Model_h
#define hifi_Model_h

#include <QBitArray>
#include <QObject>
#include <QUrl>

#include <CapsuleShape.h>

#include <AnimationCache.h>

#include "GeometryCache.h"
#include "InterfaceConfig.h"
#include "ProgramObject.h"
#include "TextureCache.h"

class AnimationHandle;
class Shape;

typedef QSharedPointer<AnimationHandle> AnimationHandlePointer;
typedef QWeakPointer<AnimationHandle> WeakAnimationHandlePointer;
    
class JointState {
public:
    JointState();

    void setFBXJoint(const FBXJoint& joint); 
    const FBXJoint& getFBXJoint() const { return *_fbxJoint; }

    void updateWorldTransform(const glm::mat4& baseTransform, const glm::quat& parentRotation);

    glm::vec3 _translation;  // translation relative to parent
    glm::quat _rotation;     // rotation relative to parent
    glm::mat4 _transform;    // rotation to world frame + translation in model frame
    glm::quat _combinedRotation; // rotation from joint local to world frame
    float _animationPriority; // the priority of the animation affecting this joint

private:
    const FBXJoint* _fbxJoint;    // JointState does not own its FBXJoint
};

/// A generic 3D model displaying geometry loaded from a URL.
class Model : public QObject {
    Q_OBJECT
    
public:

    Model(QObject* parent = NULL);
    virtual ~Model();
    
    void setTranslation(const glm::vec3& translation) { _translation = translation; }
    const glm::vec3& getTranslation() const { return _translation; }
    
    void setRotation(const glm::quat& rotation) { _rotation = rotation; }
    const glm::quat& getRotation() const { return _rotation; }
    
    /// enables/disables scale to fit behavior, the model will be automatically scaled to the specified largest dimension
    void setScaleToFit(bool scaleToFit, float largestDimension = 0.0f);
    bool getScaleToFit() const { return _scaleToFit; } /// is scale to fit enabled
    bool getIsScaledToFit() const { return _scaledToFit; } /// is model scaled to fit
    bool getScaleToFitDimension() const { return _scaleToFitLargestDimension; } /// the dimension model is scaled to

    void setSnapModelToCenter(bool snapModelToCenter);
    bool getSnapModelToCenter() { return _snapModelToCenter; }
    
    void setScale(const glm::vec3& scale);
    const glm::vec3& getScale() const { return _scale; }
    
    void setOffset(const glm::vec3& offset);
    const glm::vec3& getOffset() const { return _offset; }
    
    void setPupilDilation(float dilation) { _pupilDilation = dilation; }
    float getPupilDilation() const { return _pupilDilation; }
    
    void setBlendshapeCoefficients(const QVector<float>& coefficients) { _blendshapeCoefficients = coefficients; }
    const QVector<float>& getBlendshapeCoefficients() const { return _blendshapeCoefficients; }
    
    bool isActive() const { return _geometry && _geometry->isLoaded(); }
    
    bool isRenderable() const { return !_meshStates.isEmpty() || (isActive() && _geometry->getMeshes().isEmpty()); }
    
    bool isLoadedWithTextures() const { return _geometry && _geometry->isLoadedWithTextures(); }
    
    void init();
    void reset();
    virtual void simulate(float deltaTime, bool fullUpdate = true);
    
    enum RenderMode { DEFAULT_RENDER_MODE, SHADOW_RENDER_MODE, DIFFUSE_RENDER_MODE, NORMAL_RENDER_MODE };
    
    bool render(float alpha = 1.0f, RenderMode mode = DEFAULT_RENDER_MODE, bool receiveShadows = true);

    /// Sets the URL of the model to render.
    /// \param fallback the URL of a fallback model to render if the requested model fails to load
    /// \param retainCurrent if true, keep rendering the current model until the new one is loaded
    /// \param delayLoad if true, don't load the model immediately; wait until actually requested
    Q_INVOKABLE void setURL(const QUrl& url, const QUrl& fallback = QUrl(),
        bool retainCurrent = false, bool delayLoad = false);
    
    const QUrl& getURL() const { return _url; }
    
    /// Sets the distance parameter used for LOD computations.
    void setLODDistance(float distance) { _lodDistance = distance; }
    
    /// Returns the extents of the model in its bind pose.
    Extents getBindExtents() const;

    /// Returns the extents of the model's mesh
    Extents getMeshExtents() const;

    /// Returns the unscaled extents of the model's mesh
    Extents getUnscaledMeshExtents() const;

    /// Returns a reference to the shared geometry.
    const QSharedPointer<NetworkGeometry>& getGeometry() const { return _geometry; }
    
    /// Returns the number of joint states in the model.
    int getJointStateCount() const { return _jointStates.size(); }
    
    /// Fetches the joint state at the specified index.
    /// \return whether or not the joint state is "valid" (that is, non-default)
    bool getJointState(int index, glm::quat& rotation) const;
    
    /// Sets the joint state at the specified index.
    void setJointState(int index, bool valid, const glm::quat& rotation = glm::quat(), float priority = 1.0f);
    
    /// Returns the index of the parent of the indexed joint, or -1 if not found.
    int getParentJointIndex(int jointIndex) const;
    
    /// Returns the index of the last free ancestor of the indexed joint, or -1 if not found.
    int getLastFreeJointIndex(int jointIndex) const;
    
    bool getJointPosition(int jointIndex, glm::vec3& position) const;
    bool getJointRotation(int jointIndex, glm::quat& rotation, bool fromBind = false) const;

    QStringList getJointNames() const;
    
    AnimationHandlePointer createAnimationHandle();
    
    const QList<AnimationHandlePointer>& getRunningAnimations() const { return _runningAnimations; }
    
    void clearShapes();
    void rebuildShapes();
    void resetShapePositions();
    void updateShapePositions();
    void renderJointCollisionShapes(float alpha);
    void renderBoundingCollisionShapes(float alpha);
    
    bool findRayIntersection(const glm::vec3& origin, const glm::vec3& direction, float& distance) const;
    
    /// \param shapes list of pointers shapes to test against Model
    /// \param collisions list to store collision results
    /// \return true if at least one shape collided agains Model
    bool findCollisions(const QVector<const Shape*> shapes, CollisionList& collisions);

    bool findSphereCollisions(const glm::vec3& penetratorCenter, float penetratorRadius,
        CollisionList& collisions, int skipIndex = -1);

    bool findPlaneCollisions(const glm::vec4& plane, CollisionList& collisions);
    
    /// \param collision details about the collisions
    /// \return true if the collision is against a moveable joint
    bool collisionHitsMoveableJoint(CollisionInfo& collision) const;

    /// \param collision details about the collision
    /// Use the collision to affect the model
    void applyCollision(CollisionInfo& collision);

    float getBoundingRadius() const { return _boundingRadius; }
    float getBoundingShapeRadius() const { return _boundingShape.getRadius(); }

    /// Sets blended vertices computed in a separate thread.
    void setBlendedVertices(const QVector<glm::vec3>& vertices, const QVector<glm::vec3>& normals);

    const CapsuleShape& getBoundingShape() const { return _boundingShape; }

protected:

    QSharedPointer<NetworkGeometry> _geometry;
    
    glm::vec3 _translation;
    glm::quat _rotation;
    glm::vec3 _scale;
    glm::vec3 _offset;

    bool _scaleToFit; /// If you set scaleToFit, we will calculate scale based on MeshExtents
    float _scaleToFitLargestDimension; /// this is the dimension that scale to fit will use
    bool _scaledToFit; /// have we scaled to fit
    
    bool _snapModelToCenter; /// is the model's offset automatically adjusted to center around 0,0,0 in model space
    bool _snappedToCenter; /// are we currently snapped to center
    int _rootIndex;
    
    bool _shapesAreDirty;
    QVector<JointState> _jointStates;
    QVector<Shape*> _jointShapes;
    
    float _boundingRadius;
    CapsuleShape _boundingShape;
    glm::vec3 _boundingShapeLocalOffset;
    
    class MeshState {
    public:
        QVector<glm::mat4> clusterMatrices;
    };
    
    QVector<MeshState> _meshStates;
    
    // returns 'true' if needs fullUpdate after geometry change
    bool updateGeometry();
    
    void setScaleInternal(const glm::vec3& scale);
    void scaleToFit();
    void snapToCenter();

    void simulateInternal(float deltaTime);

    /// Updates the state of the joint at the specified index.
    virtual void updateJointState(int index);
    
    bool setJointPosition(int jointIndex, const glm::vec3& translation, const glm::quat& rotation = glm::quat(),
        bool useRotation = false, int lastFreeIndex = -1, bool allIntermediatesFree = false,
        const glm::vec3& alignment = glm::vec3(0.0f, -1.0f, 0.0f), float priority = 1.0f);
    bool setJointRotation(int jointIndex, const glm::quat& rotation, float priority = 1.0f);
    
    void setJointTranslation(int jointIndex, const glm::vec3& translation);
    
    /// Restores the indexed joint to its default position.
    /// \param percent the percentage of the default position to apply (i.e., 0.25f to slerp one fourth of the way to
    /// the original position
    /// \return true if the joint was found
    bool restoreJointPosition(int jointIndex, float percent = 1.0f, float priority = 0.0f);
    
    /// Computes and returns the extended length of the limb terminating at the specified joint and starting at the joint's
    /// first free ancestor.
    float getLimbLength(int jointIndex) const;

    void applyRotationDelta(int jointIndex, const glm::quat& delta, bool constrain = true, float priority = 1.0f);
    
    void computeBoundingShape(const FBXGeometry& geometry);

private:
    
    friend class AnimationHandle;
    
    void applyNextGeometry();
    void deleteGeometry();
    void renderMeshes(float alpha, RenderMode mode, bool translucent, bool receiveShadows);
    QVector<JointState> createJointStates(const FBXGeometry& geometry);
    
    QSharedPointer<NetworkGeometry> _baseGeometry; ///< reference required to prevent collection of base
    QSharedPointer<NetworkGeometry> _nextBaseGeometry;
    QSharedPointer<NetworkGeometry> _nextGeometry;
    float _lodDistance;
    float _lodHysteresis;
    float _nextLODHysteresis;
    
    float _pupilDilation;
    QVector<float> _blendshapeCoefficients;
    
    QUrl _url;
        
    QVector<QOpenGLBuffer> _blendedVertexBuffers;
    
    QVector<QVector<QSharedPointer<Texture> > > _dilatedTextures;
    
    QVector<Model*> _attachments;

    QSet<WeakAnimationHandlePointer> _animationHandles;

    QList<AnimationHandlePointer> _runningAnimations;

    static ProgramObject _program;
    static ProgramObject _normalMapProgram;
    static ProgramObject _specularMapProgram;
    static ProgramObject _normalSpecularMapProgram;
    
    static ProgramObject _shadowMapProgram;
    static ProgramObject _shadowNormalMapProgram;
    static ProgramObject _shadowSpecularMapProgram;
    static ProgramObject _shadowNormalSpecularMapProgram;
    
    static ProgramObject _shadowProgram;
    
    static ProgramObject _skinProgram;
    static ProgramObject _skinNormalMapProgram;
    static ProgramObject _skinSpecularMapProgram;
    static ProgramObject _skinNormalSpecularMapProgram;
    
    static ProgramObject _skinShadowMapProgram;
    static ProgramObject _skinShadowNormalMapProgram;
    static ProgramObject _skinShadowSpecularMapProgram;
    static ProgramObject _skinShadowNormalSpecularMapProgram;
    
    static ProgramObject _skinShadowProgram;
    
    static int _normalMapTangentLocation;
    static int _normalSpecularMapTangentLocation;
    static int _shadowNormalMapTangentLocation;
    static int _shadowNormalSpecularMapTangentLocation;
    
    class SkinLocations {
    public:
        int clusterMatrices;
        int clusterIndices;
        int clusterWeights;
        int tangent;
    };
    
    static SkinLocations _skinLocations;
    static SkinLocations _skinNormalMapLocations;
    static SkinLocations _skinSpecularMapLocations;
    static SkinLocations _skinNormalSpecularMapLocations;
    static SkinLocations _skinShadowMapLocations;
    static SkinLocations _skinShadowNormalMapLocations;
    static SkinLocations _skinShadowSpecularMapLocations;
    static SkinLocations _skinShadowNormalSpecularMapLocations;
    static SkinLocations _skinShadowLocations;
    
    static void initSkinProgram(ProgramObject& program, SkinLocations& locations,
        int specularTextureUnit = 1, int shadowTextureUnit = 1);
};

Q_DECLARE_METATYPE(QPointer<Model>)
Q_DECLARE_METATYPE(QWeakPointer<NetworkGeometry>)
Q_DECLARE_METATYPE(QVector<glm::vec3>)

/// Represents a handle to a model animation.
class AnimationHandle : public QObject {
    Q_OBJECT
    
public:

    void setRole(const QString& role) { _role = role; }
    const QString& getRole() const { return _role; }

    void setURL(const QUrl& url);
    const QUrl& getURL() const { return _url; }
    
    void setFPS(float fps) { _fps = fps; }
    float getFPS() const { return _fps; }

    void setPriority(float priority);
    float getPriority() const { return _priority; }
    
    void setLoop(bool loop) { _loop = loop; }
    bool getLoop() const { return _loop; }
    
    void setHold(bool hold) { _hold = hold; }
    bool getHold() const { return _hold; }
    
    void setStartAutomatically(bool startAutomatically);
    bool getStartAutomatically() const { return _startAutomatically; }
    
    void setFirstFrame(int firstFrame) { _firstFrame = firstFrame; }
    int getFirstFrame() const { return _firstFrame; }
    
    void setLastFrame(int lastFrame) { _lastFrame = lastFrame; }
    int getLastFrame() const { return _lastFrame; }
    
    void setMaskedJoints(const QStringList& maskedJoints);
    const QStringList& getMaskedJoints() const { return _maskedJoints; }
    
    void setRunning(bool running);
    bool isRunning() const { return _running; }

signals:
    
    void runningChanged(bool running);

public slots:

    void start() { setRunning(true); }
    void stop() { setRunning(false); }
    
private:

    friend class Model;

    AnimationHandle(Model* model);
        
    void simulate(float deltaTime);
    void replaceMatchingPriorities(float newPriority);
    
    Model* _model;
    WeakAnimationHandlePointer _self;
    AnimationPointer _animation;
    QString _role;
    QUrl _url;
    float _fps;
    float _priority;
    bool _loop;
    bool _hold;
    bool _startAutomatically;
    int _firstFrame;
    int _lastFrame;
    QStringList _maskedJoints;
    bool _running;
    QVector<int> _jointMappings;
    float _frameIndex;
};

#endif // hifi_Model_h
