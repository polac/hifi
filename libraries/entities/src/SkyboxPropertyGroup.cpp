//
//  SkyboxPropertyGroup.cpp
//  libraries/entities/src
//
//  Created by Brad Hefta-Gaub on 12/4/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <OctreePacketData.h>

#include "SkyboxPropertyGroup.h"
#include "EntityItemProperties.h"
#include "EntityItemPropertiesMacros.h"

SkyboxPropertyGroup::SkyboxPropertyGroup() {
    _color.red = _color.green = _color.blue = 0;
    _url = QString();
}

void SkyboxPropertyGroup::copyToScriptValue(QScriptValue& properties, QScriptEngine* engine, bool skipDefaults, EntityItemProperties& defaultEntityProperties) const {
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(Skybox, skybox, Color, color);
    COPY_GROUP_PROPERTY_TO_QSCRIPTVALUE(Skybox, skybox, URL, url);
}

void SkyboxPropertyGroup::copyFromScriptValue(const QScriptValue& object, bool& _defaultSettings) {
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(skybox, color, xColor, setColor);
    COPY_GROUP_PROPERTY_FROM_QSCRIPTVALUE(skybox, url, QString, setURL);
}

void SkyboxPropertyGroup::debugDump() const {
    qDebug() << "   SkyboxPropertyGroup: ---------------------------------------------";
    qDebug() << "       Color:" << getColor() << " has changed:" << colorChanged();
    qDebug() << "       URL:" << getURL() << " has changed:" << urlChanged();
}

bool SkyboxPropertyGroup::appentToEditPacket(OctreePacketData* packetData,                                     
                                    EntityPropertyFlags& requestedProperties,
                                    EntityPropertyFlags& propertyFlags,
                                    EntityPropertyFlags& propertiesDidntFit,
                                    int& propertyCount, 
                                    OctreeElement::AppendState& appendState) const {

    bool successPropertyFits = true;

    APPEND_ENTITY_PROPERTY(PROP_SKYBOX_COLOR, getColor());
    APPEND_ENTITY_PROPERTY(PROP_SKYBOX_URL, getURL());

    return true;
}


bool SkyboxPropertyGroup::decodeFromEditPacket(EntityPropertyFlags& propertyFlags, const unsigned char*& dataAt , int& processedBytes) {

    int bytesRead = 0;
    bool overwriteLocalData = true;

    READ_ENTITY_PROPERTY(PROP_SKYBOX_COLOR, xColor, setColor);
    READ_ENTITY_PROPERTY(PROP_SKYBOX_URL, QString, setURL);

    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_SKYBOX_COLOR, Color);
    DECODE_GROUP_PROPERTY_HAS_CHANGED(PROP_SKYBOX_URL, URL);

    processedBytes += bytesRead;

    return true;
}

void SkyboxPropertyGroup::markAllChanged() {
    _colorChanged = true;
    _urlChanged = true;
}

EntityPropertyFlags SkyboxPropertyGroup::getChangedProperties() const {
    EntityPropertyFlags changedProperties;
    
    CHECK_PROPERTY_CHANGE(PROP_SKYBOX_COLOR, color);
    CHECK_PROPERTY_CHANGE(PROP_SKYBOX_URL, url);

    return changedProperties;
}

void SkyboxPropertyGroup::getProperties(EntityItemProperties& properties) const {
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Skybox, Color, getColor);
    COPY_ENTITY_GROUP_PROPERTY_TO_PROPERTIES(Skybox, URL, getURL);
}

bool SkyboxPropertyGroup::setProperties(const EntityItemProperties& properties) {
    bool somethingChanged = false;

    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Skybox, Color, color, setColor);
    SET_ENTITY_GROUP_PROPERTY_FROM_PROPERTIES(Skybox, URL, url, setURL);

    return somethingChanged;
}

EntityPropertyFlags SkyboxPropertyGroup::getEntityProperties(EncodeBitstreamParams& params) const {
    EntityPropertyFlags requestedProperties;

    requestedProperties += PROP_SKYBOX_COLOR;
    requestedProperties += PROP_SKYBOX_URL;
    
    return requestedProperties;
}
    
void SkyboxPropertyGroup::appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params, 
                                EntityTreeElementExtraEncodeData* entityTreeElementExtraEncodeData,
                                EntityPropertyFlags& requestedProperties,
                                EntityPropertyFlags& propertyFlags,
                                EntityPropertyFlags& propertiesDidntFit,
                                int& propertyCount, 
                                OctreeElement::AppendState& appendState) const {

    bool successPropertyFits = true;

    APPEND_ENTITY_PROPERTY(PROP_SKYBOX_COLOR, getColor());
    APPEND_ENTITY_PROPERTY(PROP_SKYBOX_URL, getURL());
}

int SkyboxPropertyGroup::readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead, 
                                            ReadBitstreamToTreeParams& args,
                                            EntityPropertyFlags& propertyFlags, bool overwriteLocalData) {

    int bytesRead = 0;
    const unsigned char* dataAt = data;

    READ_ENTITY_PROPERTY(PROP_SKYBOX_COLOR, xColor, setColor);
    READ_ENTITY_PROPERTY(PROP_SKYBOX_URL, QString, setURL);

    return bytesRead;
}
