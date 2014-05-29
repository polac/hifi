//
//  Octree.h
//  libraries/octree/src
//
//  Created by Stephen Birarda on 3/13/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Octree_h
#define hifi_Octree_h

#include <set>
#include <SimpleMovingAverage.h>

class CoverageMap;
class ReadBitstreamToTreeParams;
class Octree;
class OctreeElement;
class OctreeElementBag;
class OctreePacketData;
class Shape;


#include "JurisdictionMap.h"
#include "ViewFrustum.h"
#include "OctreeElement.h"
#include "OctreeElementBag.h"
#include "OctreePacketData.h"
#include "OctreeSceneStats.h"

#include <CollisionInfo.h>

#include <QObject>
#include <QReadWriteLock>

/// derive from this class to use the Octree::recurseTreeWithOperator() method
class RecurseOctreeOperator {
public:
    virtual bool PreRecursion(OctreeElement* element) = 0;
    virtual bool PostRecursion(OctreeElement* element) = 0;
};

// Callback function, for recuseTreeWithOperation
typedef bool (*RecurseOctreeOperation)(OctreeElement* element, void* extraData);
typedef enum {GRADIENT, RANDOM, NATURAL} creationMode;

const bool NO_EXISTS_BITS         = false;
const bool WANT_EXISTS_BITS       = true;
const bool NO_COLOR               = false;
const bool WANT_COLOR             = true;
const bool COLLAPSE_EMPTY_TREE    = true;
const bool DONT_COLLAPSE          = false;
const bool NO_OCCLUSION_CULLING   = false;
const bool WANT_OCCLUSION_CULLING = true;

const int DONT_CHOP              = 0;
const int NO_BOUNDARY_ADJUST     = 0;
const int LOW_RES_MOVING_ADJUST  = 1;
const quint64 IGNORE_LAST_SENT  = 0;

#define IGNORE_SCENE_STATS       NULL
#define IGNORE_VIEW_FRUSTUM      NULL
#define IGNORE_COVERAGE_MAP      NULL
#define IGNORE_JURISDICTION_MAP  NULL

class EncodeBitstreamParams {
public:
    int maxEncodeLevel;
    int maxLevelReached;
    const ViewFrustum* viewFrustum;
    bool includeColor;
    bool includeExistsBits;
    int chopLevels;
    bool deltaViewFrustum;
    const ViewFrustum* lastViewFrustum;
    bool wantOcclusionCulling;
    int boundaryLevelAdjust;
    float octreeElementSizeScale;
    quint64 lastViewFrustumSent;
    bool forceSendScene;
    OctreeSceneStats* stats;
    CoverageMap* map;
    JurisdictionMap* jurisdictionMap;

    // output hints from the encode process
    typedef enum {
        UNKNOWN,
        DIDNT_FIT,
        NULL_NODE,
        TOO_DEEP,
        OUT_OF_JURISDICTION,
        LOD_SKIP,
        OUT_OF_VIEW,
        WAS_IN_VIEW,
        NO_CHANGE,
        OCCLUDED
    } reason;
    reason stopReason;

    EncodeBitstreamParams(
        int maxEncodeLevel = INT_MAX,
        const ViewFrustum* viewFrustum = IGNORE_VIEW_FRUSTUM,
        bool includeColor = WANT_COLOR,
        bool includeExistsBits = WANT_EXISTS_BITS,
        int  chopLevels = 0,
        bool deltaViewFrustum = false,
        const ViewFrustum* lastViewFrustum = IGNORE_VIEW_FRUSTUM,
        bool wantOcclusionCulling = NO_OCCLUSION_CULLING,
        CoverageMap* map = IGNORE_COVERAGE_MAP,
        int boundaryLevelAdjust = NO_BOUNDARY_ADJUST,
        float octreeElementSizeScale = DEFAULT_OCTREE_SIZE_SCALE,
        quint64 lastViewFrustumSent = IGNORE_LAST_SENT,
        bool forceSendScene = true,
        OctreeSceneStats* stats = IGNORE_SCENE_STATS,
        JurisdictionMap* jurisdictionMap = IGNORE_JURISDICTION_MAP) :
            maxEncodeLevel(maxEncodeLevel),
            maxLevelReached(0),
            viewFrustum(viewFrustum),
            includeColor(includeColor),
            includeExistsBits(includeExistsBits),
            chopLevels(chopLevels),
            deltaViewFrustum(deltaViewFrustum),
            lastViewFrustum(lastViewFrustum),
            wantOcclusionCulling(wantOcclusionCulling),
            boundaryLevelAdjust(boundaryLevelAdjust),
            octreeElementSizeScale(octreeElementSizeScale),
            lastViewFrustumSent(lastViewFrustumSent),
            forceSendScene(forceSendScene),
            stats(stats),
            map(map),
            jurisdictionMap(jurisdictionMap),
            stopReason(UNKNOWN)
    {}

    void displayStopReason() {
        printf("StopReason: ");
        switch (stopReason) {
            default:
            case UNKNOWN: qDebug("UNKNOWN"); break;

            case DIDNT_FIT: qDebug("DIDNT_FIT"); break;
            case NULL_NODE: qDebug("NULL_NODE"); break;
            case TOO_DEEP: qDebug("TOO_DEEP"); break;
            case OUT_OF_JURISDICTION: qDebug("OUT_OF_JURISDICTION"); break;
            case LOD_SKIP: qDebug("LOD_SKIP"); break;
            case OUT_OF_VIEW: qDebug("OUT_OF_VIEW"); break;
            case WAS_IN_VIEW: qDebug("WAS_IN_VIEW"); break;
            case NO_CHANGE: qDebug("NO_CHANGE"); break;
            case OCCLUDED: qDebug("OCCLUDED"); break;
        }
    }
};

class ReadElementBufferToTreeArgs {
public:
    const unsigned char* buffer;
    int length;
    bool destructive;
    bool pathChanged;
};

class ReadBitstreamToTreeParams {
public:
    bool includeColor;
    bool includeExistsBits;
    OctreeElement* destinationElement;
    QUuid sourceUUID;
    SharedNodePointer sourceNode;
    bool wantImportProgress;
    PacketVersion bitstreamVersion;

    ReadBitstreamToTreeParams(
        bool includeColor = WANT_COLOR,
        bool includeExistsBits = WANT_EXISTS_BITS,
        OctreeElement* destinationElement = NULL,
        QUuid sourceUUID = QUuid(),
        SharedNodePointer sourceNode = SharedNodePointer(),
        bool wantImportProgress = false,
        PacketVersion bitstreamVersion = 0) :
            includeColor(includeColor),
            includeExistsBits(includeExistsBits),
            destinationElement(destinationElement),
            sourceUUID(sourceUUID),
            sourceNode(sourceNode),
            wantImportProgress(wantImportProgress),
            bitstreamVersion(bitstreamVersion)
    {}
};

class Octree : public QObject {
    Q_OBJECT
public:
    Octree(bool shouldReaverage = false);
    ~Octree();

    /// Your tree class must implement this to create the correct element type
    virtual OctreeElement* createNewElement(unsigned char * octalCode = NULL) = 0;

    // These methods will allow the OctreeServer to send your tree inbound edit packets of your
    // own definition. Implement these to allow your octree based server to support editing
    virtual bool getWantSVOfileVersions() const { return false; }
    virtual PacketType expectedDataPacketType() const { return PacketTypeUnknown; }
    virtual bool canProcessVersion(PacketVersion thisVersion) const { 
                    return thisVersion == versionForPacketType(expectedDataPacketType()); }
    virtual PacketVersion expectedVersion() const { return versionForPacketType(expectedDataPacketType()); }
    virtual bool handlesEditPacketType(PacketType packetType) const { return false; }
    virtual int processEditPacketData(PacketType packetType, const unsigned char* packetData, int packetLength,
                    const unsigned char* editData, int maxLength, const SharedNodePointer& sourceNode) { return 0; }
                    
    virtual bool recurseChildrenWithData() const { return true; }
    virtual bool rootElementHasData() const { return false; }


    virtual void update() { }; // nothing to do by default

    OctreeElement* getRoot() { return _rootElement; }

    void eraseAllOctreeElements();

    void processRemoveOctreeElementsBitstream(const unsigned char* bitstream, int bufferSizeBytes);
    void readBitstreamToTree(const unsigned char* bitstream,  unsigned long int bufferSizeBytes, ReadBitstreamToTreeParams& args);
    void deleteOctalCodeFromTree(const unsigned char* codeBuffer, bool collapseEmptyTrees = DONT_COLLAPSE);
    void reaverageOctreeElements(OctreeElement* startElement = NULL);

    void deleteOctreeElementAt(float x, float y, float z, float s);
    
    /// Find the voxel at position x,y,z,s
    /// \return pointer to the OctreeElement or NULL if none at x,y,z,s.
    OctreeElement* getOctreeElementAt(float x, float y, float z, float s) const;
    
    /// Find the voxel at position x,y,z,s
    /// \return pointer to the OctreeElement or to the smallest enclosing parent if none at x,y,z,s.
    OctreeElement* getOctreeEnclosingElementAt(float x, float y, float z, float s) const;
    
    OctreeElement* getOrCreateChildElementAt(float x, float y, float z, float s);
    OctreeElement* getOrCreateChildElementContaining(const AACube& box);

    void recurseTreeWithOperation(RecurseOctreeOperation operation, void* extraData = NULL);
    void recurseTreeWithPostOperation(RecurseOctreeOperation operation, void* extraData = NULL);

    void recurseTreeWithOperationDistanceSorted(RecurseOctreeOperation operation,
                                                const glm::vec3& point, void* extraData = NULL);

    void recurseTreeWithOperator(RecurseOctreeOperator* operatorObject);

    int encodeTreeBitstream(OctreeElement* element, OctreePacketData* packetData, OctreeElementBag& bag,
                            EncodeBitstreamParams& params) ;

    bool isDirty() const { return _isDirty; }
    void clearDirtyBit() { _isDirty = false; }
    void setDirtyBit() { _isDirty = true; }

    // Octree does not currently handle its own locking, caller must use these to lock/unlock
    void lockForRead() { _lock.lockForRead(); }
    bool tryLockForRead() { return _lock.tryLockForRead(); }
    void lockForWrite() { _lock.lockForWrite(); }
    bool tryLockForWrite() { return _lock.tryLockForWrite(); }
    void unlock() { _lock.unlock(); }
    // output hints from the encode process
    typedef enum {
        Lock,
        TryLock,
        NoLock
    } lockType;

    bool findRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                             OctreeElement*& node, float& distance, BoxFace& face, 
                             void** intersectedObject = NULL,
                             Octree::lockType lockType = Octree::TryLock, bool* accurateResult = NULL);

    bool findSpherePenetration(const glm::vec3& center, float radius, glm::vec3& penetration, void** penetratedObject = NULL, 
                                    Octree::lockType lockType = Octree::TryLock, bool* accurateResult = NULL);

    bool findCapsulePenetration(const glm::vec3& start, const glm::vec3& end, float radius, glm::vec3& penetration, 
                                    Octree::lockType lockType = Octree::TryLock, bool* accurateResult = NULL);

    bool findShapeCollisions(const Shape* shape, CollisionList& collisions, 
                                    Octree::lockType = Octree::TryLock, bool* accurateResult = NULL);

    OctreeElement* getElementEnclosingPoint(const glm::vec3& point, 
                                    Octree::lockType lockType = Octree::TryLock, bool* accurateResult = NULL);

    // Note: this assumes the fileFormat is the HIO individual voxels code files
    void loadOctreeFile(const char* fileName, bool wantColorRandomizer);

    // these will read/write files that match the wireformat, excluding the 'V' leading
    void writeToSVOFile(const char* filename, OctreeElement* element = NULL);
    bool readFromSVOFile(const char* filename);
    

    unsigned long getOctreeElementsCount();

    void copySubTreeIntoNewTree(OctreeElement* startElement, Octree* destinationTree, bool rebaseToRoot);
    void copyFromTreeIntoSubTree(Octree* sourceTree, OctreeElement* destinationElement);

    bool getShouldReaverage() const { return _shouldReaverage; }

    void recurseElementWithOperation(OctreeElement* element, RecurseOctreeOperation operation,
                void* extraData, int recursionCount = 0);

	/// Traverse child nodes of node applying operation in post-fix order
	///
    void recurseElementWithPostOperation(OctreeElement* element, RecurseOctreeOperation operation,
                void* extraData, int recursionCount = 0);

    void recurseElementWithOperationDistanceSorted(OctreeElement* element, RecurseOctreeOperation operation,
                const glm::vec3& point, void* extraData, int recursionCount = 0);

    bool recurseElementWithOperator(OctreeElement* element, RecurseOctreeOperator* operatorObject, int recursionCount = 0);

    bool getIsViewing() const { return _isViewing; }
    void setIsViewing(bool isViewing) { _isViewing = isViewing; }
    

signals:
    void importSize(float x, float y, float z);
    void importProgress(int progress);

public slots:
    void cancelImport();


protected:
    void deleteOctalCodeFromTreeRecursion(OctreeElement* element, void* extraData);

    int encodeTreeBitstreamRecursion(OctreeElement* element,
                                     OctreePacketData* packetData, OctreeElementBag& bag,
                                     EncodeBitstreamParams& params, int& currentEncodeLevel,
                                     const ViewFrustum::location& parentLocationThisView) const;

    static bool countOctreeElementsOperation(OctreeElement* element, void* extraData);

    OctreeElement* nodeForOctalCode(OctreeElement* ancestorElement, const unsigned char* needleCode, OctreeElement** parentOfFoundElement) const;
    OctreeElement* createMissingElement(OctreeElement* lastParentElement, const unsigned char* codeToReach);
    int readElementData(OctreeElement *destinationElement, const unsigned char* nodeData,
                int bufferSizeBytes, ReadBitstreamToTreeParams& args);

    OctreeElement* _rootElement;

    bool _isDirty;
    bool _shouldReaverage;
    bool _stopImport;

    QReadWriteLock _lock;
    
    /// This tree is receiving inbound viewer datagrams.
    bool _isViewing;
};

float boundaryDistanceForRenderLevel(unsigned int renderLevel, float voxelSizeScale);

#endif // hifi_Octree_h
