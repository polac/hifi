//
//  OctreeSendThread.cpp
//  assignment-client/src/octree
//
//  Created by Brad Hefta-Gaub on 8/21/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <NodeList.h>
#include <PacketHeaders.h>
#include <PerfStat.h>
#include <SharedUtil.h>

#include "OctreeSendThread.h"
#include "OctreeServer.h"
#include "OctreeServerConsts.h"

quint64 startSceneSleepTime = 0;
quint64 endSceneSleepTime = 0;

OctreeSendThread::OctreeSendThread(const SharedAssignmentPointer& myAssignment, const SharedNodePointer& node) :
    _myAssignment(myAssignment),
    _myServer(static_cast<OctreeServer*>(myAssignment.data())),
    _node(node),
    _nodeUUID(node->getUUID()),
    _packetData(),
    _nodeMissingCount(0),
    _isShuttingDown(false)
{
    QString safeServerName("Octree");
    if (_myServer) {
        safeServerName = _myServer->getMyServerName();
    }
    qDebug() << qPrintable(safeServerName)  << "server [" << _myServer << "]: client connected "
                                            "- starting sending thread [" << this << "]";

    OctreeServer::clientConnected();
}

OctreeSendThread::~OctreeSendThread() {
    QString safeServerName("Octree");
    if (_myServer) {
        safeServerName = _myServer->getMyServerName();
    }
    
    qDebug() << qPrintable(safeServerName)  << "server [" << _myServer << "]: client disconnected "
                                            "- ending sending thread [" << this << "]";

    OctreeServer::clientDisconnected();
    OctreeServer::stopTrackingThread(this);

    _node.clear();
    _myAssignment.clear();
}

void OctreeSendThread::setIsShuttingDown() {
    _isShuttingDown = true;
}


bool OctreeSendThread::process() {
    if (_isShuttingDown) {
        return false; // exit early if we're shutting down
    }

    // check that our server and assignment is still valid
    if (!_myServer || !_myAssignment) {
        return false; // exit early if it's not, it means the server is shutting down
    }

    OctreeServer::didProcess(this);

    quint64  start = usecTimestampNow();

    // don't do any send processing until the initial load of the octree is complete...
    if (_myServer->isInitialLoadComplete()) {
        if (_node) {
            _nodeMissingCount = 0;
            OctreeQueryNode* nodeData = static_cast<OctreeQueryNode*>(_node->getLinkedData());

            // Sometimes the node data has not yet been linked, in which case we can't really do anything
            if (nodeData && !nodeData->isShuttingDown()) {
                bool viewFrustumChanged = nodeData->updateCurrentViewFrustum();
                packetDistributor(nodeData, viewFrustumChanged);
            }
        }
    }

    if (_isShuttingDown) {
        return false; // exit early if we're shutting down
    }

    // Only sleep if we're still running and we got the lock last time we tried, otherwise try to get the lock asap
    if (isStillRunning()) {
        // dynamically sleep until we need to fire off the next set of octree elements
        int elapsed = (usecTimestampNow() - start);
        int usecToSleep =  OCTREE_SEND_INTERVAL_USECS - elapsed;

        if (usecToSleep > 0) {
            PerformanceWarning warn(false,"OctreeSendThread... usleep()",false,&_usleepTime,&_usleepCalls);
            usleep(usecToSleep);
        } else {
            const int MIN_USEC_TO_SLEEP = 1;
            usleep(MIN_USEC_TO_SLEEP);
        }
    }

    return isStillRunning();  // keep running till they terminate us
}

quint64 OctreeSendThread::_usleepTime = 0;
quint64 OctreeSendThread::_usleepCalls = 0;

quint64 OctreeSendThread::_totalBytes = 0;
quint64 OctreeSendThread::_totalWastedBytes = 0;
quint64 OctreeSendThread::_totalPackets = 0;

int OctreeSendThread::handlePacketSend(OctreeQueryNode* nodeData, int& trueBytesSent, int& truePacketsSent) {

    OctreeServer::didHandlePacketSend(this);
                 
    // if we're shutting down, then exit early       
    if (nodeData->isShuttingDown()) {
        return 0;
    }
    
    bool debug = _myServer->wantsDebugSending();
    quint64 now = usecTimestampNow();

    bool packetSent = false; // did we send a packet?
    int packetsSent = 0;
    
    // Here's where we check to see if this packet is a duplicate of the last packet. If it is, we will silently
    // obscure the packet and not send it. This allows the callers and upper level logic to not need to know about
    // this rate control savings.
    if (nodeData->shouldSuppressDuplicatePacket()) {
        nodeData->resetOctreePacket(); // we still need to reset it though!
        return packetsSent; // without sending...
    }

    const unsigned char* messageData = nodeData->getPacket();
    int numBytesPacketHeader = numBytesForPacketHeader(reinterpret_cast<const char*>(messageData));
    const unsigned char* dataAt = messageData + numBytesPacketHeader;
    dataAt += sizeof(OCTREE_PACKET_FLAGS);
    OCTREE_PACKET_SEQUENCE sequence = (*(OCTREE_PACKET_SEQUENCE*)dataAt);
    dataAt += sizeof(OCTREE_PACKET_SEQUENCE);


    // If we've got a stats message ready to send, then see if we can piggyback them together
    if (nodeData->stats.isReadyToSend() && !nodeData->isShuttingDown()) {
        // Send the stats message to the client
        unsigned char* statsMessage = nodeData->stats.getStatsMessage();
        int statsMessageLength = nodeData->stats.getStatsMessageLength();
        int piggyBackSize = nodeData->getPacketLength() + statsMessageLength;

        // If the size of the stats message and the voxel message will fit in a packet, then piggyback them
        if (piggyBackSize < MAX_PACKET_SIZE) {

            // copy voxel message to back of stats message
            memcpy(statsMessage + statsMessageLength, nodeData->getPacket(), nodeData->getPacketLength());
            statsMessageLength += nodeData->getPacketLength();

            // since a stats message is only included on end of scene, don't consider any of these bytes "wasted", since
            // there was nothing else to send.
            int thisWastedBytes = 0;
            _totalWastedBytes += thisWastedBytes;
            _totalBytes += nodeData->getPacketLength();
            _totalPackets++;
            if (debug) {
                qDebug() << "Adding stats to packet at " << now << " [" << _totalPackets <<"]: sequence: " << sequence <<
                        " statsMessageLength: " << statsMessageLength <<
                        " original size: " << nodeData->getPacketLength() << " [" << _totalBytes <<
                        "] wasted bytes:" << thisWastedBytes << " [" << _totalWastedBytes << "]";
            }

            // actually send it
            OctreeServer::didCallWriteDatagram(this);
            NodeList::getInstance()->writeDatagram((char*) statsMessage, statsMessageLength, _node);
            packetSent = true;
        } else {
            // not enough room in the packet, send two packets
            OctreeServer::didCallWriteDatagram(this);
            NodeList::getInstance()->writeDatagram((char*) statsMessage, statsMessageLength, _node);

            // since a stats message is only included on end of scene, don't consider any of these bytes "wasted", since
            // there was nothing else to send.
            int thisWastedBytes = 0;
            _totalWastedBytes += thisWastedBytes;
            _totalBytes += statsMessageLength;
            _totalPackets++;
            if (debug) {
                qDebug() << "Sending separate stats packet at " << now << " [" << _totalPackets <<"]: sequence: " << sequence <<
                        " size: " << statsMessageLength << " [" << _totalBytes <<
                        "] wasted bytes:" << thisWastedBytes << " [" << _totalWastedBytes << "]";
            }

            trueBytesSent += statsMessageLength;
            truePacketsSent++;
            packetsSent++;

            OctreeServer::didCallWriteDatagram(this);
            NodeList::getInstance()->writeDatagram((char*) nodeData->getPacket(), nodeData->getPacketLength(), _node);

            packetSent = true;

            thisWastedBytes = MAX_PACKET_SIZE - nodeData->getPacketLength();
            _totalWastedBytes += thisWastedBytes;
            _totalBytes += nodeData->getPacketLength();
            _totalPackets++;
            if (debug) {
                qDebug() << "Sending packet at " << now << " [" << _totalPackets <<"]: sequence: " << sequence <<
                        " size: " << nodeData->getPacketLength() << " [" << _totalBytes <<
                        "] wasted bytes:" << thisWastedBytes << " [" << _totalWastedBytes << "]";
            }
        }
        nodeData->stats.markAsSent();
    } else {
        // If there's actually a packet waiting, then send it.
        if (nodeData->isPacketWaiting() && !nodeData->isShuttingDown()) {
            // just send the voxel packet
            OctreeServer::didCallWriteDatagram(this);
            NodeList::getInstance()->writeDatagram((char*) nodeData->getPacket(), nodeData->getPacketLength(), _node);
            packetSent = true;

            int thisWastedBytes = MAX_PACKET_SIZE - nodeData->getPacketLength();
            _totalWastedBytes += thisWastedBytes;
            _totalBytes += nodeData->getPacketLength();
            _totalPackets++;
            if (debug) {
                qDebug() << "Sending packet at " << now << " [" << _totalPackets <<"]: sequence: " << sequence <<
                        " size: " << nodeData->getPacketLength() << " [" << _totalBytes <<
                        "] wasted bytes:" << thisWastedBytes << " [" << _totalWastedBytes << "]";
            }
        }
    }
    // remember to track our stats
    if (packetSent) {
        nodeData->stats.packetSent(nodeData->getPacketLength());
        trueBytesSent += nodeData->getPacketLength();
        truePacketsSent++;
        packetsSent++;
        nodeData->incrementSequenceNumber();
        nodeData->resetOctreePacket();
    }

    return packetsSent;
}

/// Version of voxel distributor that sends the deepest LOD level at once
int OctreeSendThread::packetDistributor(OctreeQueryNode* nodeData, bool viewFrustumChanged) {
        
    OctreeServer::didPacketDistributor(this);

    // if shutting down, exit early
    if (nodeData->isShuttingDown()) {
        return 0;
    }
    
    int truePacketsSent = 0;
    int trueBytesSent = 0;
    int packetsSentThisInterval = 0;
    bool isFullScene = ((!viewFrustumChanged || !nodeData->getWantDelta()) && nodeData->getViewFrustumJustStoppedChanging()) 
                                || nodeData->hasLodChanged();

    bool somethingToSend = true; // assume we have something

    // FOR NOW... node tells us if it wants to receive only view frustum deltas
    bool wantDelta = viewFrustumChanged && nodeData->getWantDelta();

    // If our packet already has content in it, then we must use the color choice of the waiting packet.
    // If we're starting a fresh packet, then...
    //     If we're moving, and the client asked for low res, then we force monochrome, otherwise, use
    //     the clients requested color state.
    bool wantColor = nodeData->getWantColor();
    bool wantCompression = nodeData->getWantCompression();

    // If we have a packet waiting, and our desired want color, doesn't match the current waiting packets color
    // then let's just send that waiting packet.
    if (!nodeData->getCurrentPacketFormatMatches()) {
        if (nodeData->isPacketWaiting()) {
            packetsSentThisInterval += handlePacketSend(nodeData, trueBytesSent, truePacketsSent);
        } else {
            nodeData->resetOctreePacket();
        }
        int targetSize = MAX_OCTREE_PACKET_DATA_SIZE;
        if (wantCompression) {
            targetSize = nodeData->getAvailable() - sizeof(OCTREE_PACKET_INTERNAL_SECTION_SIZE);
        }
        _packetData.changeSettings(wantCompression, targetSize);
    }

    const ViewFrustum* lastViewFrustum =  wantDelta ? &nodeData->getLastKnownViewFrustum() : NULL;

    // If the current view frustum has changed OR we have nothing to send, then search against
    // the current view frustum for things to send.
    if (viewFrustumChanged || nodeData->nodeBag.isEmpty()) {

        // if our view has changed, we need to reset these things...
        if (viewFrustumChanged) {
            if (nodeData->moveShouldDump() || nodeData->hasLodChanged()) {
                nodeData->dumpOutOfView();
            }
            nodeData->map.erase();
        }

        if (!viewFrustumChanged && !nodeData->getWantDelta()) {
            // only set our last sent time if we weren't resetting due to frustum change
            quint64 now = usecTimestampNow();
            nodeData->setLastTimeBagEmpty(now);
        }

        // track completed scenes and send out the stats packet accordingly
        nodeData->stats.sceneCompleted();
        nodeData->setLastRootTimestamp(_myServer->getOctree()->getRoot()->getLastChanged());

        // TODO: add these to stats page
        //::endSceneSleepTime = _usleepTime;
        //unsigned long sleepTime = ::endSceneSleepTime - ::startSceneSleepTime;
        //unsigned long encodeTime = nodeData->stats.getTotalEncodeTime();
        //unsigned long elapsedTime = nodeData->stats.getElapsedTime();

        int packetsJustSent = handlePacketSend(nodeData, trueBytesSent, truePacketsSent);
        packetsSentThisInterval += packetsJustSent;

        // If we're starting a full scene, then definitely we want to empty the nodeBag
        if (isFullScene) {
            nodeData->nodeBag.deleteAll();
        }

        // TODO: add these to stats page
        //::startSceneSleepTime = _usleepTime;
        
        // start tracking our stats
        nodeData->stats.sceneStarted(isFullScene, viewFrustumChanged, _myServer->getOctree()->getRoot(), _myServer->getJurisdiction());

        // This is the start of "resending" the scene.
        bool dontRestartSceneOnMove = false; // this is experimental
        if (dontRestartSceneOnMove) {
            if (nodeData->nodeBag.isEmpty()) {
                nodeData->nodeBag.insert(_myServer->getOctree()->getRoot()); // only in case of empty
            }
        } else {
            nodeData->nodeBag.insert(_myServer->getOctree()->getRoot()); // original behavior, reset on move or empty
        }
    }

    // If we have something in our nodeBag, then turn them into packets and send them out...
    if (!nodeData->nodeBag.isEmpty()) {
        int bytesWritten = 0;
        quint64 start = usecTimestampNow();

        // TODO: add these to stats page
        //quint64 startCompressTimeMsecs = OctreePacketData::getCompressContentTime() / 1000;
        //quint64 startCompressCalls = OctreePacketData::getCompressContentCalls();

        int clientMaxPacketsPerInterval = std::max(1,(nodeData->getMaxOctreePacketsPerSecond() / INTERVALS_PER_SECOND));
        int maxPacketsPerInterval = std::min(clientMaxPacketsPerInterval, _myServer->getPacketsPerClientPerInterval());

        int extraPackingAttempts = 0;
        bool completedScene = false;
        while (somethingToSend && packetsSentThisInterval < maxPacketsPerInterval && !nodeData->isShuttingDown()) {
            float lockWaitElapsedUsec = OctreeServer::SKIP_TIME;
            float encodeElapsedUsec = OctreeServer::SKIP_TIME;
            float compressAndWriteElapsedUsec = OctreeServer::SKIP_TIME;
            float packetSendingElapsedUsec = OctreeServer::SKIP_TIME;
            
            quint64 startInside = usecTimestampNow();            

            bool lastNodeDidntFit = false; // assume each node fits
            if (!nodeData->nodeBag.isEmpty()) {
                OctreeElement* subTree = nodeData->nodeBag.extract();
                
                /* TODO: Looking for a way to prevent locking and encoding a tree that is not
                // going to result in any packets being sent...
                //
                // If our node is root, and the root hasn't changed, and our view hasn't changed,
                // and we've already seen at least one duplicate packet, then we probably don't need 
                // to lock the tree and encode, because the result should be that no bytes will be 
                // encoded, and this will be a duplicate packet from the  last one we sent...
                OctreeElement* root = _myServer->getOctree()->getRoot();
                bool skipEncode = false;
                if (
                        (subTree == root)
                        && (nodeData->getLastRootTimestamp() == root->getLastChanged())
                        && !viewFrustumChanged 
                        && (nodeData->getDuplicatePacketCount() > 0)
                ) {
                    qDebug() << "is root, root not changed, view not changed, already seen a duplicate!"
                        << "Can we skip it?";
                    skipEncode = true;
                }
                */

                bool wantOcclusionCulling = nodeData->getWantOcclusionCulling();
                CoverageMap* coverageMap = wantOcclusionCulling ? &nodeData->map : IGNORE_COVERAGE_MAP;
                
                float voxelSizeScale = nodeData->getOctreeSizeScale();
                int boundaryLevelAdjustClient = nodeData->getBoundaryLevelAdjust();
                
                int boundaryLevelAdjust = boundaryLevelAdjustClient + (viewFrustumChanged && nodeData->getWantLowResMoving()
                                                                       ? LOW_RES_MOVING_ADJUST : NO_BOUNDARY_ADJUST);
                
                EncodeBitstreamParams params(INT_MAX, &nodeData->getCurrentViewFrustum(), wantColor,
                                             WANT_EXISTS_BITS, DONT_CHOP, wantDelta, lastViewFrustum,
                                             wantOcclusionCulling, coverageMap, boundaryLevelAdjust, voxelSizeScale,
                                             nodeData->getLastTimeBagEmpty(),
                                             isFullScene, &nodeData->stats, _myServer->getJurisdiction());

                // TODO: should this include the lock time or not? This stat is sent down to the client,
                // it seems like it may be a good idea to include the lock time as part of the encode time
                // are reported to client. Since you can encode without the lock
                nodeData->stats.encodeStarted();
                
                quint64 lockWaitStart = usecTimestampNow();
                _myServer->getOctree()->lockForRead();
                quint64 lockWaitEnd = usecTimestampNow();
                lockWaitElapsedUsec = (float)(lockWaitEnd - lockWaitStart);

                quint64 encodeStart = usecTimestampNow();
                bytesWritten = _myServer->getOctree()->encodeTreeBitstream(subTree, &_packetData, nodeData->nodeBag, params);
                quint64 encodeEnd = usecTimestampNow();
                encodeElapsedUsec = (float)(encodeEnd - encodeStart);
                
                // If after calling encodeTreeBitstream() there are no nodes left to send, then we know we've
                // sent the entire scene. We want to know this below so we'll actually write this content into
                // the packet and send it
                completedScene = nodeData->nodeBag.isEmpty();

                // if we're trying to fill a full size packet, then we use this logic to determine if we have a DIDNT_FIT case.
                if (_packetData.getTargetSize() == MAX_OCTREE_PACKET_DATA_SIZE) {
                    if (_packetData.hasContent() && bytesWritten == 0 &&
                            params.stopReason == EncodeBitstreamParams::DIDNT_FIT) {
                        lastNodeDidntFit = true;
                    }
                } else {
                    // in compressed mode and we are trying to pack more... and we don't care if the _packetData has
                    // content or not... because in this case even if we were unable to pack any data, we want to drop
                    // below to our sendNow logic, but we do want to track that we attempted to pack extra
                    extraPackingAttempts++;
                    if (bytesWritten == 0 && params.stopReason == EncodeBitstreamParams::DIDNT_FIT) {
                        lastNodeDidntFit = true;
                    }
                }

                nodeData->stats.encodeStopped();
                _myServer->getOctree()->unlock();
            } else {
                // If the bag was empty then we didn't even attempt to encode, and so we know the bytesWritten were 0
                bytesWritten = 0;
                somethingToSend = false; // this will cause us to drop out of the loop...
            }

            // If the last node didn't fit, but we're in compressed mode, then we actually want to see if we can fit a
            // little bit more in this packet. To do this we write into the packet, but don't send it yet, we'll
            // keep attempting to write in compressed mode to add more compressed segments

            // We only consider sending anything if there is something in the _packetData to send... But
            // if bytesWritten == 0 it means either the subTree couldn't fit or we had an empty bag... Both cases
            // mean we should send the previous packet contents and reset it.
            if (completedScene || lastNodeDidntFit) {
                if (_packetData.hasContent()) {
                
                    quint64 compressAndWriteStart = usecTimestampNow();
                    
                    // if for some reason the finalized size is greater than our available size, then probably the "compressed"
                    // form actually inflated beyond our padding, and in this case we will send the current packet, then
                    // write to out new packet...
                    unsigned int writtenSize = _packetData.getFinalizedSize()
                            + (nodeData->getCurrentPacketIsCompressed() ? sizeof(OCTREE_PACKET_INTERNAL_SECTION_SIZE) : 0);


                    if (writtenSize > nodeData->getAvailable()) {
                        packetsSentThisInterval += handlePacketSend(nodeData, trueBytesSent, truePacketsSent);
                    }

                    nodeData->writeToPacket(_packetData.getFinalizedData(), _packetData.getFinalizedSize());
                    extraPackingAttempts = 0;
                    quint64 compressAndWriteEnd = usecTimestampNow();
                    compressAndWriteElapsedUsec = (float)(compressAndWriteEnd - compressAndWriteStart);
                }

                // If we're not running compressed, then we know we can just send now. Or if we're running compressed, but
                // the packet doesn't have enough space to bother attempting to pack more...
                bool sendNow = true;
                
                if (nodeData->getCurrentPacketIsCompressed() &&
                    nodeData->getAvailable() >= MINIMUM_ATTEMPT_MORE_PACKING &&
                    extraPackingAttempts <= REASONABLE_NUMBER_OF_PACKING_ATTEMPTS) {
                    sendNow = false; // try to pack more
                }

                int targetSize = MAX_OCTREE_PACKET_DATA_SIZE;
                if (sendNow) {
                    quint64 packetSendingStart = usecTimestampNow();
                    packetsSentThisInterval += handlePacketSend(nodeData, trueBytesSent, truePacketsSent);
                    quint64 packetSendingEnd = usecTimestampNow();
                    packetSendingElapsedUsec = (float)(packetSendingEnd - packetSendingStart);

                    if (wantCompression) {
                        targetSize = nodeData->getAvailable() - sizeof(OCTREE_PACKET_INTERNAL_SECTION_SIZE);
                    }
                } else {
                    // If we're in compressed mode, then we want to see if we have room for more in this wire packet.
                    // but we've finalized the _packetData, so we want to start a new section, we will do that by
                    // resetting the packet settings with the max uncompressed size of our current available space
                    // in the wire packet. We also include room for our section header, and a little bit of padding
                    // to account for the fact that whenc compressing small amounts of data, we sometimes end up with
                    // a larger compressed size then uncompressed size
                    targetSize = nodeData->getAvailable() - sizeof(OCTREE_PACKET_INTERNAL_SECTION_SIZE) - COMPRESS_PADDING;
                }
                _packetData.changeSettings(nodeData->getWantCompression(), targetSize); // will do reset

            }
            OctreeServer::trackTreeWaitTime(lockWaitElapsedUsec);
            OctreeServer::trackEncodeTime(encodeElapsedUsec);
            OctreeServer::trackCompressAndWriteTime(compressAndWriteElapsedUsec);
            OctreeServer::trackPacketSendingTime(packetSendingElapsedUsec);
            
            quint64 endInside = usecTimestampNow();
            quint64 elapsedInsideUsecs = endInside - startInside;
            OctreeServer::trackInsideTime((float)elapsedInsideUsecs);
        }


        // Here's where we can/should allow the server to send other data...
        // send the environment packet
        // TODO: should we turn this into a while loop to better handle sending multiple special packets
        if (_myServer->hasSpecialPacketToSend(_node) && !nodeData->isShuttingDown()) {
            trueBytesSent += _myServer->sendSpecialPacket(_node);
            truePacketsSent++;
            packetsSentThisInterval++;
        }


        quint64 end = usecTimestampNow();
        int elapsedmsec = (end - start)/USECS_PER_MSEC;
        OctreeServer::trackLoopTime(elapsedmsec);

        // TODO: add these to stats page
        //quint64 endCompressCalls = OctreePacketData::getCompressContentCalls();
        //int elapsedCompressCalls = endCompressCalls - startCompressCalls;
        //quint64 endCompressTimeMsecs = OctreePacketData::getCompressContentTime() / 1000;
        //int elapsedCompressTimeMsecs = endCompressTimeMsecs - startCompressTimeMsecs;

        // if after sending packets we've emptied our bag, then we want to remember that we've sent all
        // the voxels from the current view frustum
        if (nodeData->nodeBag.isEmpty()) {
            nodeData->updateLastKnownViewFrustum();
            nodeData->setViewSent(true);
            nodeData->map.erase(); // It would be nice if we could save this, and only reset it when the view frustum changes
        }

    } // end if bag wasn't empty, and so we sent stuff...

    return truePacketsSent;
}
