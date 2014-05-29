#version 120

//
//  skin_model_normal_map.vert
//  vertex shader
//
//  Created by Andrzej Kapolka on 10/29/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

const int MAX_CLUSTERS = 128;
const int INDICES_PER_VERTEX = 4;

uniform mat4 clusterMatrices[MAX_CLUSTERS];

// the tangent vector
attribute vec3 tangent;

attribute vec4 clusterIndices;
attribute vec4 clusterWeights;

// the interpolated position
varying vec4 interpolatedPosition;

// the interpolated normal
varying vec4 interpolatedNormal;

// the interpolated tangent
varying vec4 interpolatedTangent;

void main(void) {
    interpolatedPosition = vec4(0.0, 0.0, 0.0, 0.0);
    interpolatedNormal = vec4(0.0, 0.0, 0.0, 0.0);
    interpolatedTangent = vec4(0.0, 0.0, 0.0, 0.0);
    for (int i = 0; i < INDICES_PER_VERTEX; i++) {
        mat4 clusterMatrix = clusterMatrices[int(clusterIndices[i])];
        float clusterWeight = clusterWeights[i];
        interpolatedPosition += clusterMatrix * gl_Vertex * clusterWeight;
        interpolatedNormal += clusterMatrix * vec4(gl_Normal, 0.0) * clusterWeight;
        interpolatedTangent += clusterMatrix * vec4(tangent, 0.0) * clusterWeight;
    }
    interpolatedPosition = gl_ModelViewMatrix * interpolatedPosition;
    interpolatedNormal = gl_ModelViewMatrix * interpolatedNormal;
    interpolatedTangent = gl_ModelViewMatrix * interpolatedTangent;
    
    // pass along the vertex color
    gl_FrontColor = vec4(1.0, 1.0, 1.0, 1.0);
    
    // and the texture coordinates
    gl_TexCoord[0] = gl_MultiTexCoord0;
    
    // and the shadow texture coordinates
    gl_TexCoord[1] = vec4(dot(gl_EyePlaneS[0], interpolatedPosition), dot(gl_EyePlaneT[0], interpolatedPosition),
        dot(gl_EyePlaneR[0], interpolatedPosition), 1.0); 
        
    gl_Position = gl_ProjectionMatrix * interpolatedPosition;
}
