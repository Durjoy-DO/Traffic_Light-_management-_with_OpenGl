#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char* kVS = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
uniform vec2 uPos;      
uniform vec2 uScale;    
uniform mat4 uProj;     
void main(){
    vec2 p = uPos + aPos * uScale; 
    gl_Position = uProj * vec4(p, 0.0, 1.0);
}
)GLSL";

static const char* kFS = R"GLSL(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main(){ FragColor = vec4(uColor, 1.0); }
)GLSL";

static GLuint makeShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){ char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); fprintf(stderr, "Shader error: %s\n", log); }
    return s;
}

static GLuint makeProgram(){
    GLuint vs = makeShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = makeShader(GL_FRAGMENT_SHADER, kFS);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){ char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); fprintf(stderr, "Link error: %s\n", log);} 
    glDeleteShader(vs); glDeleteShader(fs); return p;
}

class Ortho {
public:
    float l=-20, r=20, b=-12, t=12;
    float mat[16]{};
    
    void update(){
        float rl = r-l, tb=t-b, fn=100.f;
        float m[16] = {
            2/rl,0,0,0,
            0,2/tb,0,0,
            0,0,-2/fn,0,
            -(r+l)/rl, -(t+b)/tb, -(100+0)/fn, 1
        };
        std::copy(m,m+16,mat);
    }
};

enum class LightState { RED, YELLOW, GREEN };

class IndividualLight {
public:
    LightState state = LightState::RED;
    float timer = 0.0f;
    float greenTime = 7.0f;
    float yellowTime = 2.0f;
    bool manual = false;
    
    void setState(LightState s) { state = s; timer = 0.0f; }
    
    void update(float dt) {
        if(manual) return;
        timer += dt;
        if(state == LightState::GREEN && timer >= greenTime) {
            state = LightState::YELLOW;
            timer = 0.0f;
        } else if(state == LightState::YELLOW && timer >= yellowTime) {
            state = LightState::RED;
            timer = 0.0f;
        }
    }
};

class TrafficLightSystem {
public:
    IndividualLight north, south, east, west;
    bool manual = false;
    bool emergencyMode = false;
    float emergencyTimer = 0.0f;
    
    void setManual(bool on) { 
        manual = on; 
        north.manual = on; 
        south.manual = on; 
        east.manual = on; 
        west.manual = on; 
    }
    
    void setEmergencyMode(bool on) {
        emergencyMode = on;
        emergencyTimer = 0.0f;
    }
    
    void update(float dt) {
        if(emergencyMode) {
            emergencyTimer += dt;
            if(emergencyTimer > 30.0f) {
                emergencyMode = false;
                printf("Emergency mode auto-cleared after 30 seconds\n");
            }
        }
        if(!manual && !emergencyMode) {
            static float cycleTimer = 0.0f;
            static int currentAxis = 0;
            cycleTimer += dt;
            if(cycleTimer > 10.0f) {
                if(currentAxis == 0) {
                    north.setState(LightState::RED);
                    south.setState(LightState::RED);
                    east.setState(LightState::GREEN);
                    west.setState(LightState::GREEN);
                    currentAxis = 1;
                } else {
                    east.setState(LightState::RED);
                    west.setState(LightState::RED);
                    north.setState(LightState::GREEN);
                    south.setState(LightState::GREEN);
                    currentAxis = 0;
                }
                cycleTimer = 0.0f;
            }
        } else {
            north.update(dt);
            south.update(dt);
            east.update(dt);
            west.update(dt);
        }
    }
    
    bool nsProceed() const { return north.state == LightState::GREEN || south.state == LightState::GREEN; }
    bool ewProceed() const { return east.state == LightState::GREEN || west.state == LightState::GREEN; }
};

class Car {
public:
    float x=0, y=0; 
    float vx=0, vy=0; 
    float speed=6.0f; 
    float w=1.6f, h=0.9f; 
    bool active=true;
    int lane=0; 
    char axis='N'; 
    
    void update(float dt){ x += vx*speed*dt; y += vy*speed*dt; }
};

class World {
public:
    Ortho cam;
    GLuint prog=0, vao=0, vbo=0;
    TrafficLightSystem light;
    std::vector<Car> cars;
    float spawnIntervalNS = 2.2f;
    float spawnIntervalEW = 2.2f;
    float spawnTimerNS = 0.f;
    float spawnTimerEW = 0.f;
    bool paused=false;
    std::mt19937 rng{12345};
    const float stopNS = 2.5f; 
    const float stopEW = 4.0f; 
    const float roadHalf = 3.0f; 
    
    void initGL(){
        prog = makeProgram();
        glUseProgram(prog);
        float verts[] = { -1,-1, 1,-1, -1,1, 1,1 };
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        cam.update();
    }
    
    void drawRect(float cx, float cy, float hw, float hh, float r, float g, float b){
        glUseProgram(prog);
        GLint locP = glGetUniformLocation(prog, "uProj");
        GLint locPos = glGetUniformLocation(prog, "uPos");
        GLint locScale = glGetUniformLocation(prog, "uScale");
        GLint locColor = glGetUniformLocation(prog, "uColor");
        glUniformMatrix4fv(locP, 1, GL_FALSE, cam.mat);
        glUniform2f(locPos, cx, cy);
        glUniform2f(locScale, hw, hh);
        glUniform3f(locColor, r,g,b);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }
    
    void drawCircle(float cx, float cy, float radius, float r, float g, float b){
        const int rings = 8;
        const int segments = 16;
        for(int ring = 0; ring < rings; ring++){
            float ringRadius = radius * (ring + 1) / rings;
            float rectSize = radius * 0.15f;
            for(int i = 0; i < segments; i++){
                float angle = (2.0f * M_PI * i) / segments;
                float x = cx + std::cos(angle) * ringRadius;
                float y = cy + std::sin(angle) * ringRadius;
                drawRect(x, y, rectSize, rectSize, r, g, b);
            }
        }
        drawRect(cx, cy, radius * 0.4f, radius * 0.4f, r, g, b);
    }
    
    void drawTrafficLight(float cx, float cy, bool isVertical, LightState state){
        float boxW = isVertical ? .8f : 1.5f;
        float boxH = isVertical ? 1.5f : 0.8f;
        drawRect(cx, cy, boxW, boxH, 0.05f, 0.05f, 0.05f); 
        float frameW = boxW + 0.1f;
        float frameH = boxH + 0.1f;
        drawRect(cx, cy, frameW, frameH, 0.15f, 0.15f, 0.15f); 
        drawRect(cx, cy, boxW, boxH, 0.02f, 0.02f, 0.02f); 
        bool redOn = (state == LightState::RED);
        bool yellowOn = (state == LightState::YELLOW);
        bool greenOn = (state == LightState::GREEN);
        float lightRadius = 0.28f;
        if(isVertical){
            drawCircle(cx, cy + 0.9f, lightRadius, 
                      redOn ? 1.0f : 0.2f,     
                      redOn ? 0.0f : 0.05f,    
                      redOn ? 0.0f : 0.05f);   
            drawCircle(cx, cy, lightRadius,
                      yellowOn ? 1.0f : 0.25f,   
                      yellowOn ? 0.8f : 0.15f,   
                      yellowOn ? 0.0f : 0.05f);  
            drawCircle(cx, cy - 0.9f, lightRadius,
                      greenOn ? 0.0f : 0.05f,    
                      greenOn ? 1.0f : 0.2f,     
                      greenOn ? 0.0f : 0.05f);   
        } else {
            drawCircle(cx - 0.9f, cy, lightRadius,
                      redOn ? 1.0f : 0.2f,
                      redOn ? 0.0f : 0.05f,
                      redOn ? 0.0f : 0.05f);
            drawCircle(cx, cy, lightRadius,
                      yellowOn ? 1.0f : 0.25f,
                      yellowOn ? 0.8f : 0.15f,
                      yellowOn ? 0.0f : 0.05f);
            drawCircle(cx + 0.9f, cy, lightRadius,
                      greenOn ? 0.0f : 0.05f,
                      greenOn ? 1.0f : 0.2f,
                      greenOn ? 0.0f : 0.05f);
        }
        float highlightRadius = lightRadius * 0.3f;
        if(isVertical){
            if(redOn) drawCircle(cx - 0.08f, cy + 0.9f + 0.08f, highlightRadius, 1.0f, 0.8f, 0.8f);
            if(yellowOn) drawCircle(cx - 0.08f, cy + 0.08f, highlightRadius, 1.0f, 1.0f, 0.8f);
            if(greenOn) drawCircle(cx - 0.08f, cy - 0.9f + 0.08f, highlightRadius, 0.8f, 1.0f, 0.8f);
        } else {
            if(redOn) drawCircle(cx - 0.9f - 0.08f, cy + 0.08f, highlightRadius, 1.0f, 0.8f, 0.8f);
            if(yellowOn) drawCircle(cx - 0.08f, cy + 0.08f, highlightRadius, 1.0f, 1.0f, 0.8f);
            if(greenOn) drawCircle(cx + 0.9f - 0.08f, cy + 0.08f, highlightRadius, 0.8f, 1.0f, 0.8f);
        }
    }
    
    void drawCarDetailed(float cx, float cy, float hw, float hh, char direction, int lane, float r, float g, float b){
        bool isVertical = (direction == 'N' || direction == 'S');
        drawRect(cx, cy, hw, hh, r, g, b);
        float highlightW = hw * 0.8f;
        float highlightH = hh * 0.8f;
        drawRect(cx, cy, highlightW, highlightH, r + 0.1f, g + 0.1f, b + 0.1f);
        float windowW = hw * (isVertical ? 0.7f : 0.5f);
        float windowH = hh * (isVertical ? 0.5f : 0.7f);
        drawRect(cx, cy, windowW, windowH, 0.2f, 0.3f, 0.4f);
        if(isVertical) {
            float frontY = (direction == 'N') ? cy + hh * 0.3f : cy - hh * 0.3f;
            drawRect(cx, frontY, windowW, windowH * 0.4f, 0.3f, 0.4f, 0.5f);
        } else {
            float frontX = (direction == 'E') ? cx + hw * 0.3f : cx - hw * 0.3f;
            drawRect(frontX, cy, windowW * 0.4f, windowH, 0.3f, 0.4f, 0.5f);
        }
        float wheelSize = std::min(hw, hh) * 0.12f;
        if(isVertical) {
            drawCircle(cx - hw * 0.8f, cy + hh * 0.35f, wheelSize, 0.1f, 0.1f, 0.1f);
            drawCircle(cx + hw * 0.8f, cy + hh * 0.35f, wheelSize, 0.1f, 0.1f, 0.1f);
            drawCircle(cx - hw * 0.8f, cy - hh * 0.35f, wheelSize, 0.1f, 0.1f, 0.1f);
            drawCircle(cx + hw * 0.8f, cy - hh * 0.35f, wheelSize, 0.1f, 0.1f, 0.1f);
            float rimSize = wheelSize * 0.6f;
            drawCircle(cx - hw * 0.8f, cy + hh * 0.35f, rimSize, 0.4f, 0.4f, 0.4f);
            drawCircle(cx + hw * 0.8f, cy + hh * 0.35f, rimSize, 0.4f, 0.4f, 0.4f);
            drawCircle(cx - hw * 0.8f, cy - hh * 0.35f, rimSize, 0.4f, 0.4f, 0.4f);
            drawCircle(cx + hw * 0.8f, cy - hh * 0.35f, rimSize, 0.4f, 0.4f, 0.4f);
        } else {
            drawCircle(cx - hw * 0.35f, cy + hh * 0.8f, wheelSize, 0.1f, 0.1f, 0.1f);
            drawCircle(cx - hw * 0.35f, cy - hh * 0.8f, wheelSize, 0.1f, 0.1f, 0.1f);
            drawCircle(cx + hw * 0.35f, cy + hh * 0.8f, wheelSize, 0.1f, 0.1f, 0.1f);
            drawCircle(cx + hw * 0.35f, cy - hh * 0.8f, wheelSize, 0.1f, 0.1f, 0.1f);
            float rimSize = wheelSize * 0.6f;
            drawCircle(cx - hw * 0.35f, cy + hh * 0.8f, rimSize, 0.4f, 0.4f, 0.4f);
            drawCircle(cx - hw * 0.35f, cy - hh * 0.8f, rimSize, 0.4f, 0.4f, 0.4f);
            drawCircle(cx + hw * 0.35f, cy + hh * 0.8f, rimSize, 0.4f, 0.4f, 0.4f);
            drawCircle(cx + hw * 0.35f, cy - hh * 0.8f, rimSize, 0.4f, 0.4f, 0.4f);
        }
        
        float stripeR = (lane == 0) ? 0.2f : 0.8f;  
        float stripeG = (lane == 0) ? 0.8f : 0.2f;
        float stripeB = 0.3f;
        if(isVertical) {
            float stripeX = (lane == 0) ? cx - hw * 0.9f : cx + hw * 0.9f;
            drawRect(stripeX, cy, hw * 0.1f, hh * 0.6f, stripeR, stripeG, stripeB);
        } else {
            float stripeY = (lane == 0) ? cy - hh * 0.9f : cy + hh * 0.9f;
            drawRect(cx, stripeY, hw * 0.6f, hh * 0.1f, stripeR, stripeG, stripeB);
        }
    }
    
    void drawWorld(){
        drawRect(0,0, 20, roadHalf, 0.18f,0.18f,0.18f); 
        drawRect(0,0, roadHalf, 12, 0.18f,0.18f,0.18f); 
        float y=-12; while(y<12){ 
            drawRect(0,y,0.05f, 0.35f, 1,1,0); 
            y+=0.7f; 
        }
        float x=-20; while(x<20){ 
            drawRect(x,0, 0.35f,0.05f, 1,1,0); 
            x+=0.7f; 
        }
        y=-12; while(y<12){ drawRect(-2.0f,y,0.03f, 0.3f, 1,1,1); y+=0.6f; }
        y=-12; while(y<12){ drawRect(2.0f,y,0.03f, 0.3f, 1,1,1); y+=0.6f; }
        x=-20; while(x<20){ drawRect(x,-2.0f, 0.3f,0.03f, 1,1,1); x+=0.6f; }
        x=-20; while(x<20){ drawRect(x,2.0f, 0.3f,0.03f, 1,1,1); x+=0.6f; }
        drawRect(0, stopNS, roadHalf, 0.06f, 1,0,0);
        drawRect(0,-stopNS, roadHalf, 0.06f, 1,0,0);
        drawRect(-stopEW, 0, 0.06f, roadHalf, 1,0,0);
        drawRect( stopEW, 0, 0.06f, roadHalf, 1,0,0);
         
        drawTrafficLight(-3.0f, -3.5f, true, light.north.state);
        drawTrafficLight(3.0f, 3.5f, true, light.south.state);     
        drawTrafficLight(-5.5f, -3.0f, false, light.east.state); 
        drawTrafficLight(5.5f, 3.0f, false, light.west.state);   
        for(const auto& c : cars){ 
            if(!c.active) continue; 
            float carR = 0.3f + (c.x * 0.1f) - floor(c.x * 0.1f);  
            float carG = 0.4f + (c.y * 0.15f) - floor(c.y * 0.15f);
            float carB = 0.5f + ((c.x + c.y) * 0.1f) - floor((c.x + c.y) * 0.1f);
            carR = std::max(0.2f, std::min(0.9f, carR));
            carG = std::max(0.2f, std::min(0.9f, carG));
            carB = std::max(0.2f, std::min(0.9f, carB));
            drawCarDetailed(c.x, c.y, c.w*0.5f, c.h*0.5f, c.axis, c.lane, carR, carG, carB); 
        }
        drawRect(-18.5f,10.5f, 1.5f,0.7f, light.manual?1.f:0.1f, light.manual?0.5f:0.8f, 0.1f);
        if(light.emergencyMode) {
            float flash = sin(glfwGetTime() * 6.0f) * 0.5f + 0.5f; 
            drawRect(-15.5f, 10.5f, 2.0f, 0.7f, 1.0f, flash * 0.3f, flash * 0.3f);
        }
    }
    
    bool hasFrontCarTooClose(const Car& me) const {
        const float headway = 1.8f; 
        for(const auto& c : cars){
            if(!c.active || &c==&me) continue;
            if(c.axis!=me.axis || c.lane!=me.lane) continue;
            if(me.vx>0 && std::abs(c.y-me.y)<0.8f && c.x>me.x && (c.x - me.x) < (me.w+headway)) return true;
            if(me.vx<0 && std::abs(c.y-me.y)<0.8f && c.x<me.x && (me.x - c.x) < (me.w+headway)) return true;
            if(me.vy>0 && std::abs(c.x-me.x)<0.8f && c.y>me.y && (c.y - me.y) < (me.h+headway)) return true;
            if(me.vy<0 && std::abs(c.x-me.x)<0.8f && c.y<me.y && (me.y - c.y) < (me.h+headway)) return true;
        }
        return false;
    }
    
    bool shouldStopAtSignal(const Car& c) const {
        const float stopGap = 1.6f; 
        const float goOnYellowThreshold = 1.0f; 
        const float interHalfX = 1.5f, interHalfY = 1.5f;
        if(std::abs(c.x) < interHalfX && std::abs(c.y) < interHalfY) return false;
        if(c.axis=='N'){ 
            float dist = (-stopNS) - c.y; 
            if(dist < -0.5f) return false; 
            if(light.north.state == LightState::GREEN) return false; 
            if(light.north.state == LightState::YELLOW){ return !(dist <= goOnYellowThreshold); }
            return dist <= stopGap;
        } else if(c.axis=='S'){ 
            float dist = c.y - stopNS; 
            if(dist < -0.5f) return false;
            if(light.south.state == LightState::GREEN) return false;
            if(light.south.state == LightState::YELLOW){ return !(dist <= goOnYellowThreshold); }
            return dist <= stopGap;
        } else if(c.axis=='E'){ 
            float dist = (-stopEW) - c.x; 
            if(dist < -0.5f) return false; 
            if(light.east.state == LightState::GREEN) return false; 
            if(light.east.state == LightState::YELLOW){ return !(dist <= goOnYellowThreshold); }
            return dist <= stopGap;
        } else if(c.axis=='W'){ 
            float dist = c.x - stopEW; 
            if(dist < -0.5f) return false;
            if(light.west.state == LightState::GREEN) return false;
            if(light.west.state == LightState::YELLOW){ return !(dist <= goOnYellowThreshold); }
            return dist <= stopGap;
        }
        return false;
    }
    
    void cullCars(){
        cars.erase(std::remove_if(cars.begin(), cars.end(), [&](const Car& c){
            return (std::abs(c.x)>22 || std::abs(c.y)>14) || !c.active; }), cars.end());
    }
    
    void spawnCars(float dt){
        spawnTimerNS += dt; spawnTimerEW += dt;
        if(spawnTimerNS >= spawnIntervalNS){
            spawnTimerNS = 0.f;
            Car cN; cN.lane=0; cN.axis='N'; cN.active=true;
            cN.x = -1.0f; cN.y = -12.5f; cN.vx=0; cN.vy=1; 
            Car cS; cS.lane=1; cS.axis='S'; cS.active=true;
            cS.x = 1.0f; cS.y = 12.5f; cS.vx=0; cS.vy=-1; 
            bool okN=true, okS=true;
            for(const auto& o: cars){
                if(!o.active) continue;
                if(o.axis=='N' && o.lane==0 && std::abs(o.x-cN.x)<0.8f && (cN.y - o.y) < 4.0f) okN=false;
                if(o.axis=='S' && o.lane==1 && std::abs(o.x-cS.x)<0.8f && (o.y - cS.y) < 4.0f) okS=false;
            }
            if(okN) cars.push_back(cN);
            if(okS) cars.push_back(cS);
        }
        if(spawnTimerEW >= spawnIntervalEW){
            spawnTimerEW = 0.f;
            Car cE; cE.lane=0; cE.axis='E'; cE.active=true;
            cE.y = -1.0f; cE.x = -20.5f; cE.vx=1; cE.vy=0; 
            Car cW; cW.lane=1; cW.axis='W'; cW.active=true;
            cW.y = 1.0f; cW.x = 20.5f; cW.vx=-1; cW.vy=0; 
            bool okE=true, okW=true;
            for(const auto& o: cars){
                if(!o.active) continue;
                if(o.axis=='E' && o.lane==0 && std::abs(o.y-cE.y)<0.8f && (o.x - cE.x) < 6.0f) okE=false;
                if(o.axis=='W' && o.lane==1 && std::abs(o.y-cW.y)<0.8f && (cW.x - o.x) < 6.0f) okW=false;
            }
            if(okE) cars.push_back(cE);
            if(okW) cars.push_back(cW);
        }
    }
    
    void update(float dt){
        if(paused) return;
        light.update(dt);
        spawnCars(dt);
        for(auto &c : cars){
            if(!c.active) continue;
            bool stop = shouldStopAtSignal(c) || hasFrontCarTooClose(c);
            if(!stop) c.update(dt);
            if(std::abs(c.x)>22 || std::abs(c.y)>14) c.active=false;
        }
        cullCars();
    }
};

static World* gWorld = nullptr;

static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods){
    if(action==GLFW_PRESS){
        if(key==GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win,1);
        if(key==GLFW_KEY_P) gWorld->paused = !gWorld->paused;
        if(key==GLFW_KEY_M){ 
            gWorld->light.setManual(!gWorld->light.manual); 
            printf("Traffic Light: %s mode\n", gWorld->light.manual ? "Manual" : "Automatic");
        }
        if(key==GLFW_KEY_A){ 
            gWorld->light.setManual(false); 
            printf("Traffic Light: Automatic mode\n");
        }
        if (gWorld->light.manual) {
            if (key == GLFW_KEY_UP && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.north.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY: North lane GREEN for emergency vehicle\n");
            }
            else if (key == GLFW_KEY_UP) { 
                LightState current = gWorld->light.north.state;
                if (current == LightState::RED) gWorld->light.north.setState(LightState::YELLOW);
                else if (current == LightState::YELLOW) gWorld->light.north.setState(LightState::GREEN);
                else gWorld->light.north.setState(LightState::RED);
                printf("North light: %s\n", current == LightState::RED ? "YELLOW" : 
                       current == LightState::YELLOW ? "GREEN" : "RED");
            }
            if (key == GLFW_KEY_DOWN && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.south.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY: South lane GREEN for emergency vehicle\n");
            }
            else if (key == GLFW_KEY_DOWN) { 
                LightState current = gWorld->light.south.state;
                if (current == LightState::RED) gWorld->light.south.setState(LightState::YELLOW);
                else if (current == LightState::YELLOW) gWorld->light.south.setState(LightState::GREEN);
                else gWorld->light.south.setState(LightState::RED);
                printf("South light: %s\n", current == LightState::RED ? "YELLOW" : 
                       current == LightState::YELLOW ? "GREEN" : "RED");
            }
            if (key == GLFW_KEY_RIGHT && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.east.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY: East lane GREEN for emergency vehicle\n");
            }
            else if (key == GLFW_KEY_RIGHT) { 
                LightState current = gWorld->light.east.state;
                if (current == LightState::RED) gWorld->light.east.setState(LightState::YELLOW);
                else if (current == LightState::YELLOW) gWorld->light.east.setState(LightState::GREEN);
                else gWorld->light.east.setState(LightState::RED);
                printf("East light: %s\n", current == LightState::RED ? "YELLOW" : 
                       current == LightState::YELLOW ? "GREEN" : "RED");
            }
            if (key == GLFW_KEY_LEFT && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.west.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY: West lane GREEN for emergency vehicle\n");
            }
            else if (key == GLFW_KEY_LEFT) { 
                LightState current = gWorld->light.west.state;
                if (current == LightState::RED) gWorld->light.west.setState(LightState::YELLOW);
                else if (current == LightState::YELLOW) gWorld->light.west.setState(LightState::GREEN);
                else gWorld->light.west.setState(LightState::RED);
                printf("West light: %s\n", current == LightState::RED ? "YELLOW" : 
                       current == LightState::YELLOW ? "GREEN" : "RED");
            }
            if (key == GLFW_KEY_1) { 
                gWorld->light.north.setState(LightState::RED);
                printf("North light: RED\n");
            }
            if (key == GLFW_KEY_2) { 
                gWorld->light.north.setState(LightState::YELLOW);
                printf("North light: YELLOW\n");
            }
            if (key == GLFW_KEY_3) { 
                gWorld->light.north.setState(LightState::GREEN);
                printf("North light: GREEN\n");
            }
            if (key == GLFW_KEY_4) { 
                gWorld->light.south.setState(LightState::RED);
                printf("South light: RED\n");
            }
            if (key == GLFW_KEY_5) { 
                gWorld->light.south.setState(LightState::YELLOW);
                printf("South light: YELLOW\n");
            }
            if (key == GLFW_KEY_6) { 
                gWorld->light.south.setState(LightState::GREEN);
                printf("South light: GREEN\n");
            }
            if (key == GLFW_KEY_Q) { 
                gWorld->light.east.setState(LightState::RED);
                printf("East light: RED\n");
            }
            if (key == GLFW_KEY_W) { 
                gWorld->light.east.setState(LightState::YELLOW);
                printf("East light: YELLOW\n");
            }
            if (key == GLFW_KEY_E) { 
                gWorld->light.east.setState(LightState::GREEN);
                printf("East light: GREEN\n");
            }
            if (key == GLFW_KEY_Z) { 
                gWorld->light.west.setState(LightState::RED);
                printf("West light: RED\n");
            }
            if (key == GLFW_KEY_X) { 
                gWorld->light.west.setState(LightState::YELLOW);
                printf("West light: YELLOW\n");
            }
            if (key == GLFW_KEY_C) { 
                gWorld->light.west.setState(LightState::GREEN);
                printf("West light: GREEN\n");
            }
            if (key == GLFW_KEY_R) { 
                gWorld->light.north.setState(LightState::RED);
                gWorld->light.south.setState(LightState::RED);
                gWorld->light.east.setState(LightState::RED);
                gWorld->light.west.setState(LightState::RED);
                printf("EMERGENCY STOP: All lights RED\n");
            }
            if (key == GLFW_KEY_G) { 
                gWorld->light.north.setState(LightState::GREEN);
                gWorld->light.south.setState(LightState::GREEN);
                gWorld->light.east.setState(LightState::GREEN);
                gWorld->light.west.setState(LightState::GREEN);
                printf("CAUTION: All lights GREEN (use carefully!)\n");
            }
            if (key == GLFW_KEY_ESCAPE && mods == GLFW_MOD_SHIFT) {
                gWorld->light.setEmergencyMode(false);
                printf("Emergency mode cleared\n");
            }
        }
        else {
            if (key == GLFW_KEY_UP && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.setManual(true);
                gWorld->light.north.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY OVERRIDE: Manual mode activated, North lane GREEN\n");
            }
            if (key == GLFW_KEY_DOWN && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.setManual(true);
                gWorld->light.south.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY OVERRIDE: Manual mode activated, South lane GREEN\n");
            }
            if (key == GLFW_KEY_RIGHT && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.setManual(true);
                gWorld->light.east.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY OVERRIDE: Manual mode activated, East lane GREEN\n");
            }
            if (key == GLFW_KEY_LEFT && mods == GLFW_MOD_SHIFT) { 
                gWorld->light.setManual(true);
                gWorld->light.west.setState(LightState::GREEN);
                gWorld->light.setEmergencyMode(true);
                printf("EMERGENCY OVERRIDE: Manual mode activated, West lane GREEN\n");
            }
        }
        if(key==GLFW_KEY_EQUAL){ gWorld->spawnIntervalNS = std::max(0.6f, gWorld->spawnIntervalNS-0.2f); gWorld->spawnIntervalEW = std::max(0.6f, gWorld->spawnIntervalEW-0.2f); }
        if(key==GLFW_KEY_MINUS){ gWorld->spawnIntervalNS += 0.2f; gWorld->spawnIntervalEW += 0.2f; }
    }
}

int main(){
    printf("=== Traffic Light Management System ===\n");
    printf("Controls:\n");
    printf("  M - Toggle Manual/Automatic mode\n");
    printf("  A - Set to Automatic mode\n");
    printf("  P - Pause/Unpause simulation\n");
    printf("  ESC - Exit\n");
    printf("\nEMERGENCY CONTROLS (works in any mode):\n");
    printf("  Shift + Arrow Keys - Emergency override for single lane:\n");
    printf("    Shift+UP    - North lane GREEN (emergency vehicle)\n");
    printf("    Shift+DOWN  - South lane GREEN (emergency vehicle)\n");
    printf("    Shift+RIGHT - East lane GREEN (emergency vehicle)\n");
    printf("    Shift+LEFT  - West lane GREEN (emergency vehicle)\n");
    printf("\nMANUAL MODE CONTROLS:\n");
    printf("  Arrow Keys (cycle through states):\n");
    printf("    UP/DOWN  - Control North/South lights\n");
    printf("    LEFT/RIGHT - Control East/West lights\n");
    printf("\n  Number Keys (North/South):\n");
    printf("    1,2,3 - North: Red, Yellow, Green\n");
    printf("    4,5,6 - South: Red, Yellow, Green\n");
    printf("\n  Letter Keys (East/West):\n");
    printf("    Q,W,E - East: Red, Yellow, Green\n");
    printf("    Z,X,C - West: Red, Yellow, Green\n");
    printf("\n  Safety Controls:\n");
    printf("    R - EMERGENCY STOP (all lights RED)\n");
    printf("    G - All lights GREEN (use with caution!)\n");
    printf("\nTraffic Controls:\n");
    printf("  +/- keys - Adjust car spawn rate\n");
    printf("========================================\n\n");
    if(!glfwInit()){ fprintf(stderr, "Failed to init GLFW\n"); return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Traffic Light Management (GLFW+GLAD)", nullptr, nullptr);
    if(!win){ fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
        fprintf(stderr, "Failed to init GLAD\n"); return -1; }
    World world; gWorld = &world; world.initGL();
    glfwSetKeyCallback(win, keyCallback);
    double last = glfwGetTime();
    while(!glfwWindowShouldClose(win)){
        double now = glfwGetTime();
        float dt = float(now - last); last = now;
        glfwPollEvents();
        world.update(dt);
        int w,h; glfwGetFramebufferSize(win,&w,&h);
        glViewport(0,0,w,h);
        glClearColor(0.08f,0.09f,0.11f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        world.drawWorld();
        glfwSwapBuffers(win);
    }
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
