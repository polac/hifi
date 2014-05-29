//
//  OctreeQueryNode.h
//  assignment-client/src/octree
//
//  Created by Brad Hefta-Gaub on 12/4/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_OctreeQueryNode_h
#define hifi_OctreeQueryNode_h

#include <iostream>


#include <CoverageMap.h>
#include <NodeData.h>
#include <OctreeConstants.h>
#include <OctreeElementBag.h>
#include <OctreePacketData.h>
#include <OctreeQuery.h>
#include <OctreeSceneStats.h>
#include <ThreadedAssignment.h> // for SharedAssignmentPointer

class OctreeSendThread;

class OctreeQueryNode : public OctreeQuery {
    Q_OBJECT
public:
    OctreeQueryNode();
    virtual ~OctreeQueryNode();

    void init(); // called after creation to set up some virtual items
    virtual PacketType getMyPacketType() const = 0;

    void resetOctreePacket();  // resets octree packet to after "V" header

    void writeToPacket(const unsigned char* buffer, unsigned int bytes); // writes to end of packet

    const unsigned char* getPacket() const { return _octreePacket; }
    unsigned int getPacketLength() const { return (MAX_PACKET_SIZE - _octreePacketAvailableBytes); }
    bool isPacketWaiting() const { return _octreePacketWaiting; }

    bool packetIsDuplicate() const;
    bool shouldSuppressDuplicatePacket();

    unsigned int getAvailable() const { return _octreePacketAvailableBytes; }
    int getMaxSearchLevel() const { return _maxSearchLevel; }
    void resetMaxSearchLevel() { _maxSearchLevel = 1; }
    void incrementMaxSearchLevel() { _maxSearchLevel++; }

    int getMaxLevelReached() const { return _maxLevelReachedInLastSearch; }
    void setMaxLevelReached(int maxLevelReached) { _maxLevelReachedInLastSearch = maxLevelReached; }

    OctreeElementBag nodeBag;
    CoverageMap map;

    ViewFrustum& getCurrentViewFrustum() { return _currentViewFrustum; }
    ViewFrustum& getLastKnownViewFrustum() { return _lastKnownViewFrustum; }
    
    // These are not classic setters because they are calculating and maintaining state
    // which is set asynchronously through the network receive
    bool updateCurrentViewFrustum();
    void updateLastKnownViewFrustum();

    bool getViewSent() const { return _viewSent; }
    void setViewSent(bool viewSent);

    bool getViewFrustumChanging() const { return _viewFrustumChanging; }
    bool getViewFrustumJustStoppedChanging() const { return _viewFrustumJustStoppedChanging; }

    bool moveShouldDump() const;

    quint64 getLastTimeBagEmpty() const { return _lastTimeBagEmpty; }
    void setLastTimeBagEmpty(quint64 lastTimeBagEmpty) { _lastTimeBagEmpty = lastTimeBagEmpty; }

    bool getCurrentPacketIsColor() const { return _currentPacketIsColor; }
    bool getCurrentPacketIsCompressed() const { return _currentPacketIsCompressed; }
    bool getCurrentPacketFormatMatches() {
        return (getCurrentPacketIsColor() == getWantColor() && getCurrentPacketIsCompressed() == getWantCompression());
    }

    bool hasLodChanged() const { return _lodChanged; };
    
    OctreeSceneStats stats;
    
    void initializeOctreeSendThread(const SharedAssignmentPointer& myAssignment, const SharedNodePointer& node);
    bool isOctreeSendThreadInitalized() { return _octreeSendThread; }
    
    void dumpOutOfView();
    
    quint64 getLastRootTimestamp() const { return _lastRootTimestamp; }
    void setLastRootTimestamp(quint64 timestamp) { _lastRootTimestamp = timestamp; }
    unsigned int getlastOctreePacketLength() const { return _lastOctreePacketLength; }
    int getDuplicatePacketCount() const { return _duplicatePacketCount; }
    
    void nodeKilled();
    void forceNodeShutdown();
    bool isShuttingDown() const { return _isShuttingDown; }

    void incrementSequenceNumber();
    
private slots:
    void sendThreadFinished();
    
private:
    OctreeQueryNode(const OctreeQueryNode &);
    OctreeQueryNode& operator= (const OctreeQueryNode&);
    
    bool _viewSent;
    unsigned char* _octreePacket;
    unsigned char* _octreePacketAt;
    unsigned int _octreePacketAvailableBytes;
    bool _octreePacketWaiting;

    unsigned char* _lastOctreePacket;
    unsigned int _lastOctreePacketLength;
    int _duplicatePacketCount;
    quint64 _firstSuppressedPacket;

    int _maxSearchLevel;
    int _maxLevelReachedInLastSearch;
    ViewFrustum _currentViewFrustum;
    ViewFrustum _lastKnownViewFrustum;
    quint64 _lastTimeBagEmpty;
    bool _viewFrustumChanging;
    bool _viewFrustumJustStoppedChanging;
    bool _currentPacketIsColor;
    bool _currentPacketIsCompressed;

    OctreeSendThread* _octreeSendThread;

    // watch for LOD changes
    int _lastClientBoundaryLevelAdjust;
    float _lastClientOctreeSizeScale;
    bool _lodChanged;
    bool _lodInitialized;

    OCTREE_PACKET_SEQUENCE _sequenceNumber;

    quint64 _lastRootTimestamp;
    
    PacketType _myPacketType;
    bool _isShuttingDown;
};

#endif // hifi_OctreeQueryNode_h
