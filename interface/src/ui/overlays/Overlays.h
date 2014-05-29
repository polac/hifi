//
//  Overlays.h
//  interface/src/ui/overlays
//
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Overlays_h
#define hifi_Overlays_h

#include <QScriptValue>

#include "Overlay.h"

class Overlays : public QObject {
    Q_OBJECT
public:
    Overlays();
    ~Overlays();
    void init(QGLWidget* parent);
    void update(float deltatime);
    void render3D();
    void render2D();

public slots:
    /// adds an overlay with the specific properties
    unsigned int addOverlay(const QString& type, const QScriptValue& properties);

    /// edits an overlay updating only the included properties, will return the identified OverlayID in case of
    /// successful edit, if the input id is for an unknown overlay this function will have no effect
    bool editOverlay(unsigned int id, const QScriptValue& properties);

    /// deletes a particle
    void deleteOverlay(unsigned int id);

    /// returns the top most overlay at the screen point, or 0 if not overlay at that point
    unsigned int getOverlayAtPoint(const glm::vec2& point);

private:
    QMap<unsigned int, Overlay*> _overlays2D;
    QMap<unsigned int, Overlay*> _overlays3D;
    QList<Overlay*> _overlaysToDelete;
    unsigned int _nextOverlayID;
    QGLWidget* _parent;
    QReadWriteLock _lock;
    QReadWriteLock _deleteLock;
};

 
#endif // hifi_Overlays_h
