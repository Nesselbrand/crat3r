#version 450


//TODO: Assignment 2.2
// Remove the hard-coded vertex data and replace it with two inputs for the position and UV coordiate.
// Then update the main function to use these vertex inputs.

//TODO: Assignment 2.4
// Add a push constant input containing a single mat4 for the model view projection matrix.
// Then, apply it to the vertex position before outputting it to the rasterizer.

//vec2 positions[3] = vec2[](
//    vec2(0.0, -0.5),
//    vec2(0.5, 0.5),
//    vec2(-0.5, 0.5)
//);
//
//vec2 uvs[3] = vec2[](
//    vec2(1.0, 0.0),
//    vec2(0.0, 1.0),
//    vec2(1.0, 1.0)
//);

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 fragUv;


layout(push_constant) uniform VertexPushConstants { mat4 MVP; } pushConstants;

void main() {
    gl_Position = pushConstants.MVP * vec4(inPosition, 1.0);
    fragUv = inUv;
}
