//
//  FBXReader.h
//  interface/src/renderer
//
//  Created by Andrzej Kapolka on 9/18/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_FBXReader_h
#define hifi_FBXReader_h

#include <QMetaType>
#include <QUrl>
#include <QVarLengthArray>
#include <QVariant>
#include <QVector>

#include <Shape.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class FBXNode;

typedef QList<FBXNode> FBXNodeList;

/// The names of the blendshapes expected by Faceshift, terminated with an empty string.
extern const char* FACESHIFT_BLENDSHAPES[];

/// The names of the joints in the Maya HumanIK rig, terminated with an empty string.
extern const char* HUMANIK_JOINTS[];

class Extents {
public:
    /// set minimum and maximum to FLT_MAX and -FLT_MAX respectively
    void reset();

    /// \param extents another intance of extents
    /// expand current limits to contain other extents
    void addExtents(const Extents& extents);

    /// \param point new point to compare against existing limits
    /// compare point to current limits and expand them if necessary to contain point
    void addPoint(const glm::vec3& point);

    /// \param point
    /// \return true if point is within current limits
    bool containsPoint(const glm::vec3& point) const;

    /// \return whether or not the extents are empty
    bool isEmpty() const { return minimum == maximum; }
    bool isValid() const { return !((minimum == glm::vec3(FLT_MAX)) && (maximum == glm::vec3(-FLT_MAX))); }

    glm::vec3 minimum;
    glm::vec3 maximum;
};

/// A node within an FBX document.
class FBXNode {
public:
    
    QByteArray name;
    QVariantList properties;
    FBXNodeList children;
};

/// A single blendshape extracted from an FBX document.
class FBXBlendshape {
public:
    
    QVector<int> indices;
    QVector<glm::vec3> vertices;
    QVector<glm::vec3> normals;
};

/// A single joint (transformation node) extracted from an FBX document.
class FBXJoint {
public:

    bool isFree;
    QVector<int> freeLineage;
    int parentIndex;
    float distanceToParent;
    float boneRadius;
    glm::vec3 translation;
    glm::mat4 preTransform;
    glm::quat preRotation;
    glm::quat rotation;
    glm::quat postRotation;
    glm::mat4 postTransform;
    glm::mat4 transform;
    glm::vec3 rotationMin;  // radians
    glm::vec3 rotationMax;  // radians
    glm::quat inverseDefaultRotation;
    glm::quat inverseBindRotation;
    glm::mat4 bindTransform;
    QString name;
    glm::vec3 shapePosition;  // in joint frame
    glm::quat shapeRotation;  // in joint frame
    Shape::Type shapeType;
};


/// A single binding to a joint in an FBX document.
class FBXCluster {
public:
    
    int jointIndex;
    glm::mat4 inverseBindMatrix;
};

/// A texture map in an FBX document.
class FBXTexture {
public:
    
    QByteArray filename;
    QByteArray content;
};

/// A single part of a mesh (with the same material).
class FBXMeshPart {
public:
    
    QVector<int> quadIndices;
    QVector<int> triangleIndices;
    
    glm::vec3 diffuseColor;
    glm::vec3 specularColor;
    float shininess;
    
    FBXTexture diffuseTexture;
    FBXTexture normalTexture;
    FBXTexture specularTexture;
};

/// A single mesh (with optional blendshapes) extracted from an FBX document.
class FBXMesh {
public:
    
    QVector<FBXMeshPart> parts;
    
    QVector<glm::vec3> vertices;
    QVector<glm::vec3> normals;
    QVector<glm::vec3> tangents;
    QVector<glm::vec3> colors;
    QVector<glm::vec2> texCoords;
    QVector<glm::vec4> clusterIndices;
    QVector<glm::vec4> clusterWeights;
    
    QVector<FBXCluster> clusters;

    Extents meshExtents;
    
    bool isEye;
    
    QVector<FBXBlendshape> blendshapes;
    
    bool hasSpecularTexture() const;
};

/// A single animation frame extracted from an FBX document.
class FBXAnimationFrame {
public:
    
    QVector<glm::quat> rotations;
};

Q_DECLARE_METATYPE(FBXAnimationFrame)
Q_DECLARE_METATYPE(QVector<FBXAnimationFrame>)

/// An attachment to an FBX document.
class FBXAttachment {
public:
    
    int jointIndex;
    QUrl url;
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;
};

/// A set of meshes extracted from an FBX document.
class FBXGeometry {
public:

    QString author;
    QString applicationName; ///< the name of the application that generated the model

    QVector<FBXJoint> joints;
    QHash<QString, int> jointIndices; ///< 1-based, so as to more easily detect missing indices
    
    QVector<FBXMesh> meshes;
    
    glm::mat4 offset;
    
    int leftEyeJointIndex;
    int rightEyeJointIndex;
    int neckJointIndex;
    int rootJointIndex;
    int leanJointIndex;
    int headJointIndex;
    int leftHandJointIndex;
    int rightHandJointIndex;
    
    QVector<int> humanIKJointIndices;
    
    glm::vec3 palmDirection;
    
    glm::vec3 neckPivot;
    
    Extents bindExtents;
    Extents meshExtents;
    
    QVector<FBXAnimationFrame> animationFrames;
    
    QVector<FBXAttachment> attachments;
    
    int getJointIndex(const QString& name) const { return jointIndices.value(name) - 1; }
    QStringList getJointNames() const;
    
    bool hasBlendedMeshes() const;
};

Q_DECLARE_METATYPE(FBXGeometry)

/// Reads an FST mapping from the supplied data.
QVariantHash readMapping(const QByteArray& data);

/// Writes an FST mapping to a byte array.
QByteArray writeMapping(const QVariantHash& mapping);

/// Reads FBX geometry from the supplied model and mapping data.
/// \exception QString if an error occurs in parsing
FBXGeometry readFBX(const QByteArray& model, const QVariantHash& mapping);

/// Reads SVO geometry from the supplied model data.
FBXGeometry readSVO(const QByteArray& model);

void calculateRotatedExtents(Extents& extents, const glm::quat& rotation);

#endif // hifi_FBXReader_h
