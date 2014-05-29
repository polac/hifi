//
//  PendingAssignedNodeData.h
//  domain-server/src
//
//  Created by Stephen Birarda on 2014-05-20.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_PendingAssignedNodeData_h
#define hifi_PendingAssignedNodeData_h

#include <QtCore/QObject>
#include <QtCore/QUuid>

class PendingAssignedNodeData : public QObject {
    Q_OBJECT
public:
    PendingAssignedNodeData(const QUuid& assignmentUUID, const QUuid& walletUUID);
    
    void setAssignmentUUID(const QUuid& assignmentUUID) { _assignmentUUID = assignmentUUID; }
    const QUuid& getAssignmentUUID() const { return _assignmentUUID; }
    
    void setWalletUUID(const QUuid& walletUUID) { _walletUUID = walletUUID; }
    const QUuid& getWalletUUID() const { return _walletUUID; }
private:
    QUuid _assignmentUUID;
    QUuid _walletUUID;
};

#endif // hifi_PendingAssignedNodeData_h