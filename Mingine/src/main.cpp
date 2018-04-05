#include <chrono>
#include <string>

#ifdef _WIN32
#include <SDL.h>
#include <lua.hpp>
#include <Box2D.h>
#else
#include <SDL2/SDL.h>
#include <lua5.3/lua.hpp>
#include <Box2D/Box2D.h>
#endif

#include "platform.h"
#include "assetDatabase.h"
#include "tmxToLua.h"
#include "box2dDebugDraw.h"
using namespace mingine;

const int FPS = 60;
const int FRAME_TIME_NS = (1000 / FPS) * 1000 * 1000;

// swap the following two lines if you want to ignore
// lua features and write your code natively.
const char * const CONFIG_FILE = "assets/scripts/core/noop.lua";
//const char * const CONFIG_FILE = "config.lua";

namespace mingine {
    extern const int NUM_SDL_SCANCODES = 512;
    extern bool prevKeys[NUM_SDL_SCANCODES];
    extern bool keys[NUM_SDL_SCANCODES];
    extern char stringBuilderBuffer[MAX_STRING];
}

// the entire game state lives here
lua_State* luaState;
AssetDatabase assetDatabase;
int mouseX = -1;
int mouseY = -1;
bool quit = false;

void error(const char* message)
{
    log(message);
    showErrorBox(message);
    quit = true;
}

void printError(lua_State* state)
{
    error(lua_tostring(state, -1));
    lua_pop(state, 1); // remove error message from top of stack
}

void setGlobal(const char* globalName, int value)
{
    lua_pushinteger(luaState, value);
    lua_setglobal(luaState, globalName);
}

void runScript(lua_State* state, const char* file)
{
    int result = luaL_loadfile(state, file);

    if (result == LUA_OK)
    {
        // use pcall to execute the script.
        result = lua_pcall(state, 0, LUA_MULTRET, 0);

        if (result != LUA_OK)
        {
            printError(state);
        }
    }
    else
    {
        printError(state);
    }
}

void call(lua_State* state, const char* functionName)
{
    // pushes onto the stack the value of the global name of the lua function to be called.
    int type = lua_getglobal(state, functionName);

    if (type == LUA_TNIL)
    {
        snprintf(stringBuilderBuffer, sizeof(stringBuilderBuffer), "Attempted to call undefined Lua function: %s", functionName);
        error(stringBuilderBuffer);
    }
    else if (lua_pcall(state, 0, 0, 0) != 0)
    {
        printError(state);
    }
}

int loadAsset(lua_State* state, AssetType assetType)
{
    LoadParameters loadParameters;
    loadParameters.path = lua_tostring(state, 1);
    loadParameters.assetType = assetType;

    std::string errorMessage("");
    int id = assetDatabase.add(loadParameters, errorMessage);

    if (errorMessage != "")
    {
        error(errorMessage.c_str());
    }

    lua_pushnumber(state, id);
    return 1;
}

#ifdef __cplusplus
extern "C" {
#endif

int LoadImage(lua_State* state)
{
    return loadAsset(state, AssetImage);
}

int LoadSound(lua_State* state)
{
    return loadAsset(state, AssetSound);
}

int LoadMusic(lua_State* state)
{
    return loadAsset(state, AssetMusic);
}

int LoadFont(lua_State* state)
{
    LoadParameters loadParameters;
    loadParameters.path = lua_tostring(state, 1);
    loadParameters.size = (int)(lua_tointeger(state, 2));
    loadParameters.assetType = AssetFont;

    std::string errorMessage("");
    int id = assetDatabase.add(loadParameters, errorMessage);

    if (errorMessage != "")
    {
        error(errorMessage.c_str());
    }

    lua_pushnumber(state, id);
    return 1;
}

int GetDrawColor(lua_State* state)
{
    RenderParameters renderParameters;

    getRenderState(Render::DrawColor, renderParameters);

    lua_pushinteger(state, renderParameters.u8[0]);
    lua_pushinteger(state, renderParameters.u8[1]);
    lua_pushinteger(state, renderParameters.u8[2]);
    lua_pushinteger(state, renderParameters.u8[3]);
    return 4;
}

int GetDrawLogicalSize(lua_State* state)
{
    RenderParameters renderParameters;

    getRenderState(Render::LogicalSize, renderParameters);

    lua_pushinteger(state, renderParameters.i[0]);
    lua_pushinteger(state, renderParameters.i[1]);
    return 2;
}

int GetDrawScale(lua_State* state)
{
    RenderParameters renderParameters;

    getRenderState(Render::Scale, renderParameters);

    lua_pushnumber(state, renderParameters.f[0]);
    lua_pushnumber(state, renderParameters.f[1]);
    return 2;
}

int SetDrawColor(lua_State* state)
{
    RenderParameters renderParameters;

    renderParameters.u8[0] = (uint8_t)lua_tointeger(state, 1);
    renderParameters.u8[1] = (uint8_t)lua_tointeger(state, 2);
    renderParameters.u8[2] = (uint8_t)lua_tointeger(state, 3);
    renderParameters.u8[3] = (uint8_t)lua_tointeger(state, 4);

    setRenderState(Render::DrawColor, renderParameters);
    return 0;
}

int SetDrawLogicalSize(lua_State* state)
{
    RenderParameters renderParameters;

    renderParameters.i[0] = (int)lua_tointeger(state, 1);
    renderParameters.i[1] = (int)lua_tointeger(state, 2);

    setRenderState(Render::LogicalSize, renderParameters);
    return 0;
}

int SetDrawScale(lua_State* state)
{
    RenderParameters renderParameters;

    renderParameters.f[0] = (float)lua_tonumber(state, 1);
    renderParameters.f[1] = (float)lua_tonumber(state, 2);

    setRenderState(Render::Scale, renderParameters);
    return 0;
}

int DrawImage(lua_State* state)
{
    // the number of function arguments is the index of the topmost value
    // on the stack. Neat.
    int numArguments = lua_gettop(state);

    int id = (int)lua_tointeger(state, 1);
    int x = (int)lua_tonumber(state, 2);
    int y = (int)lua_tonumber(state, 3);

    double angle = 0;
    if (numArguments >= 4)
    {
        angle = lua_tonumber(state, 4);
    }

    double scale = 1.0;
    if (numArguments >= 5)
    {
        scale = lua_tonumber(state, 5);
    }

    uint8_t r = 0xff;
    uint8_t g = 0xff;
    uint8_t b = 0xff;

    if (numArguments >= 8)
    {
        r = (uint8_t)lua_tointeger(state, 6);
        g = (uint8_t)lua_tointeger(state, 7);
        b = (uint8_t)lua_tointeger(state, 8);
    }

    Image* image = assetDatabase.get<Image>(id);
    image->draw(x, y, angle, scale, r, g, b);

    return 0;
}

int DrawImageFrame(lua_State* state)
{
    int id = (int)lua_tonumber(state, 1);
    int x = (int)lua_tonumber(state, 2);
    int y = (int)lua_tonumber(state, 3);
    int width = (int)lua_tonumber(state, 4);
    int height = (int)lua_tonumber(state, 5);
    int frame = (int)lua_tonumber(state, 6);

    int numArguments = lua_gettop(state);

    double angle = 0;
    if (numArguments >= 7)
    {
        angle = lua_tonumber(state, 7);
    }

    double scale = 1.0;
    if (numArguments >= 8)
    {
        scale = lua_tonumber(state, 8);
    }

    uint8_t r = 0xff;
    uint8_t g = 0xff;
    uint8_t b = 0xff;

    if (numArguments >= 11)
    {
        r = (uint8_t)lua_tointeger(state, 9);
        g = (uint8_t)lua_tointeger(state, 10);
        b = (uint8_t)lua_tointeger(state, 11);
    }
    
    Image* image = assetDatabase.get<Image>(id);
    image->drawFrame(x, y, width, height, frame, angle, scale, r, g, b);

    return 0;
}

int DrawText(lua_State* state)
{
    const char* text = lua_tostring(state, 1);
    int x = (int)lua_tonumber(state, 2);
    int y = (int)lua_tonumber(state, 3);
    int fontId = (int)lua_tonumber(state, 4);
    uint8_t r = (uint8_t)lua_tonumber(state, 5);
    uint8_t g = (uint8_t)lua_tonumber(state, 6);
    uint8_t b = (uint8_t)lua_tonumber(state, 7);

    Font* font = assetDatabase.get<Font>(fontId);
    font->draw(text, x, y, r, g, b);
        
    return 0;
}

int DrawPoint(lua_State* state)
{
    int x = (int)lua_tointeger(state, 1);
    int y = (int)lua_tointeger(state, 2);
    
    drawPoint(x, y);
    return 0;
}

int DrawLine(lua_State* state)
{
    int startX = (int)lua_tonumber(state, 1);
    int startY = (int)lua_tonumber(state, 2);
    int endX = (int)lua_tonumber(state, 3);
    int endY = (int)lua_tonumber(state, 4);
    
    drawLine(startX, startY, endX, endY);
    return 0;
}

int DrawCircle(lua_State* state)
{
    int x = (int)lua_tonumber(state, 1);
    int y = (int)lua_tonumber(state, 2);
    int radius = (int)lua_tonumber(state, 3);
        
    drawCircle(x, y, radius);
    return 0;
}

int DrawRect(lua_State* state)
{
    int x = (int)lua_tonumber(state, 1);
    int y = (int)lua_tonumber(state, 2);
    int w = (int)lua_tonumber(state, 3);
    int h = (int)lua_tonumber(state, 4);
    
    drawRect(x, y, w, h);
    return 0;
}

int FillRect(lua_State* state)
{
    int x = (int)lua_tonumber(state, 1);
    int y = (int)lua_tonumber(state, 2);
    int w = (int)lua_tonumber(state, 3);
    int h = (int)lua_tonumber(state, 4);
    
    fillRect(x, y, w, h);
    return 0;
}

int PlaySound(lua_State* state)
{
    int id = (int)lua_tointeger(state, 1);
    Sound* sound = assetDatabase.get<Sound>(id);
    sound->play();
    
    return 0;
}

int PlayMusic(lua_State* state)
{
    int id = (int)lua_tointeger(state, 1);
    Music* music = assetDatabase.get<Music>(id);
    music->play();

    return 0;
}

int StopMusic(lua_State* state)
{
    stopMusic();
    return 0;
}

int IsKeyDown(lua_State* state)
{
    int scancode = (int)lua_tointeger(state, 1);
    lua_pushboolean(state, keys[scancode]);
    return 1;
}

int IsKeyReleased(lua_State* state)
{
    int scancode = (int)lua_tointeger(state, 1);
    bool is = keys[scancode];
    bool was = prevKeys[scancode];
    
    lua_pushboolean(state, !is && was);
    return 1;
}

int IsKeyPressed(lua_State* state)
{
    int scancode = (int)lua_tointeger(state, 1);
    bool is = keys[scancode];
    bool was = prevKeys[scancode];

    lua_pushboolean(state, is && !was);
    return 1;
}

int IsMouseButtonDown(lua_State* state)
{
    int buttonId = (int)lua_tointeger(state, 1);
    lua_pushboolean(state, isMouseButtonDown(buttonId));
    return 1;
}

int GetMousePosition(lua_State* state)
{
    lua_pushnumber(state, mouseX);
    lua_pushnumber(state, mouseY);
    return 2;
}

int SetWindowTitle(lua_State* state)
{
    const char* title = lua_tostring(state, 1);
    setWindowTitle(title);

    return 0;
}

int CreateWindow(lua_State* state)
{
    int width = (int)lua_tointeger(state, 1);
    int height = (int)lua_tointeger(state, 2);

    bool fullscreen = false;
    if (lua_gettop(state) >= 3)
    {
        fullscreen = lua_toboolean(state, 3) == 1;
    }

    if ( !initPlatform(width, height, fullscreen) )
    {
        freePlatform();
    }

    return 0;
}

int Log(lua_State* state)
{
    const char* text = lua_tostring(state, 1);
    log(text);
    return 0;
}

int Quit(lua_State* state)
{
    state;
    quit = true;
    return 0;
}

int GetFrameTime(lua_State* state)
{
    lua_pushnumber(state, 1000 / FPS);
    return 1;
}

int ClearScreen(lua_State* state)
{
    uint8_t r = (uint8_t)lua_tointeger(state, 1);
    uint8_t g = (uint8_t)lua_tointeger(state, 2);
    uint8_t b = (uint8_t)lua_tointeger(state, 3);
    clearScreen(r, g, b);
    return 0;
}

int SetAssetBasePath(lua_State* state)
{
    const char* path = lua_tostring(state, 1);
    assetDatabase.setBasePath(path);

    return 0;
}

int LoadTmxFile(lua_State* state)
{
    const char* tmxFile = lua_tostring(state, 1);
    std::string s = "";

    readTmx(tmxFile, "images", "map", s);
    luaL_dostring(state, s.c_str());
        
    return 0;
}

#ifdef __cplusplus
} // end of extern "C"
#endif

// move these to a separate header/implmentation if
// you plan to do serious work in them, just to keep
// main focused on binding with lua.
void Start();
void Update();
void Draw();

int main(int argc, char* argv[])
{
    // we need these parameters for SDLmain, but some compilers will 
    // generate a warning if we don't use them. So we're "using them."
    argc, argv;

    luaState = luaL_newstate();
    luaL_openlibs(luaState);

    // make these functions available to lua scripts.
    lua_register(luaState, "LoadImage", LoadImage);
    lua_register(luaState, "LoadFont", LoadFont);
    lua_register(luaState, "LoadSound", LoadSound);
    lua_register(luaState, "LoadMusic", LoadMusic);
    lua_register(luaState, "GetDrawColor", GetDrawColor);
    lua_register(luaState, "GetDrawLogicalSize", GetDrawLogicalSize);
    lua_register(luaState, "GetDrawScale", GetDrawScale);
    lua_register(luaState, "SetDrawColor", SetDrawColor);
    lua_register(luaState, "SetDrawLogicalSize", SetDrawLogicalSize);
    lua_register(luaState, "SetDrawScale", SetDrawScale);
    lua_register(luaState, "DrawImage", DrawImage);
    lua_register(luaState, "DrawImageFrame", DrawImageFrame);
    lua_register(luaState, "DrawText", DrawText);
    lua_register(luaState, "DrawPoint", DrawPoint);
    lua_register(luaState, "DrawLine", DrawLine);
    lua_register(luaState, "DrawCircle", DrawCircle);
    lua_register(luaState, "DrawRect", DrawRect);
    lua_register(luaState, "FillRect", FillRect);
    lua_register(luaState, "PlaySound", PlaySound);
    lua_register(luaState, "PlayMusic", PlayMusic);
    lua_register(luaState, "StopMusic", StopMusic);
    lua_register(luaState, "IsKeyDown", IsKeyDown);
    lua_register(luaState, "IsKeyReleased", IsKeyReleased);
    lua_register(luaState, "IsKeyPressed", IsKeyPressed);
    lua_register(luaState, "IsMouseButtonDown", IsMouseButtonDown);
    lua_register(luaState, "GetMousePosition", GetMousePosition);
    lua_register(luaState, "SetWindowTitle", SetWindowTitle);
    lua_register(luaState, "CreateWindow", CreateWindow);
    lua_register(luaState, "Log", Log);
    lua_register(luaState, "Quit", Quit);
    lua_register(luaState, "ClearScreen", ClearScreen);
    lua_register(luaState, "GetFrameTime", GetFrameTime);
    lua_register(luaState, "SetAssetBasePath", SetAssetBasePath);
    lua_register(luaState, "LoadTmxFile", LoadTmxFile);
        
    runScript(luaState, CONFIG_FILE);
    call(luaState, "Start");
    Start();
        
    // after http://gameprogrammingpatterns.com/game-loop.html
    using namespace std::chrono;
    auto previousTime = high_resolution_clock::now();
    long long behind = 0;

    while (pollEvents(setGlobal) && !quit)
    {
        auto currentTime = high_resolution_clock::now();
        auto delta = duration_cast<nanoseconds>(currentTime - previousTime);
        behind += delta.count();
        previousTime = currentTime;
                
        // probably should be while, but causes
        // noticable frame skips with low res art
        if (behind >= FRAME_TIME_NS)
        {
            updateInput(&mouseX, &mouseY);
            call(luaState, "Update");
            Update();
            endUpdate();

            behind -= FRAME_TIME_NS;
        }
        
        beginFrame();
        call(luaState, "Draw");
        Draw();
        presentFrame();
        //presentFrameRotating();
    }
       
    stopMusic();
    lua_close(luaState);		
    assetDatabase.clear();
    freePlatform();	
    return 0;
}

/*

// Move the code below to a separate .h/.cpp if
// you plan to do serious work in native code. This will
// keep your code separated from main.cpp, which is focused
// on the game loop and lua integration. 

void Start()
{
    if (!initPlatform(1024, 768, false))
    {
        freePlatform();
    }

	setWindowTitle("Native Mingine Example");
}

void Update()
{
	// do nothing
}

void Draw()
{
    //clearScreen(68, 136, 204);
}

*/

// box2d hello world demo
// This code is taken more or less verbatim
// from the HelloWorld project provided with Box2D 2.3

float32 timeStep = 1.0f / FPS;
int32 velocityIterations = 6;
int32 positionIterations = 2;

b2Vec2 gravity(0.0f, -10.0f);
b2World world(gravity);
b2Body* body = nullptr;

box2dDebugDraw dd;

void Start()
{
	int screenWidth = 1024;
	int screenHeight = 768;

	if (!initPlatform(screenWidth, screenHeight, false))
	{
		freePlatform();
	}

	setWindowTitle("Box 2D Example");

	uint32 flags = b2Draw::e_shapeBit;
	
	//uncomment to see bounding boxes.
	//flags += b2Draw::e_aabbBit;
	
	dd.SetFlags(flags);

	dd.camera.x = screenWidth / 2;
	dd.camera.y = screenHeight / 2;

	world.SetDebugDraw(&dd);

	// Define the ground body.
	b2BodyDef groundBodyDef;
	groundBodyDef.position.Set(0.0f, -10.0f);

	// Call the body factory which allocates memory for the ground body
	// from a pool and creates the ground box shape (also from a pool).
	// The body is also added to the world.
	b2Body* groundBody = world.CreateBody(&groundBodyDef);

	// Define the ground box shape.
	b2PolygonShape groundBox;

	// The extents are the half-widths of the box.
	groundBox.SetAsBox(50.0f, 10.0f);

	// Add the ground fixture to the ground body.
	groundBody->CreateFixture(&groundBox, 0.0f);

	// Define the dynamic body. We set its position and call the body factory.
	b2BodyDef bodyDef;
	bodyDef.type = b2_dynamicBody;
	bodyDef.position.Set(0, 10.0f);
	bodyDef.angle = 3.14f / 3;
	body = world.CreateBody(&bodyDef);

	// Define another box shape for our dynamic body.
	b2PolygonShape dynamicBox;
	dynamicBox.SetAsBox(1.0f, 1.0f);

	// Define the dynamic body fixture.
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &dynamicBox;
	fixtureDef.density = 1.0f;
	fixtureDef.friction = 0.6f;
	
	// Add the shape to the body.
	body->CreateFixture(&fixtureDef);
}

void Update()
{
	// Instruct the world to perform a single step of simulation.
	// It is generally best to keep the time step and iterations fixed.
	world.Step(timeStep, velocityIterations, positionIterations);

	// Now print the position and angle of the body.
	b2Vec2 position = body->GetPosition();
	float32 angle = body->GetAngle();
		
	snprintf(stringBuilderBuffer, sizeof(stringBuilderBuffer), "%4.2f %4.2f %4.2f\n", position.x, position.y, angle);
	log(stringBuilderBuffer);
}

void Draw()
{
	clearScreen(68, 136, 204);
	world.DrawDebugData();
}