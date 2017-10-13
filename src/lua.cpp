#include "lua.h"
#include "midi.h"
#include "util.h"

#ifdef WITH_ALSA
#include "alsa.h"
#endif//WITH_ALSA

#ifdef WITH_JACK
#include "jack.h"
#endif//WITH_JACK

#ifdef WITH_XORG
#include "x11.h"
#endif//WITH_XORG

#include <string>

//FIXME ZOMG this is so ugly.
namespace m2i {
    #ifdef WITH_ALSA
    extern AlsaSeq alsa;
    #endif//WITH_ALSA

    #ifdef WITH_JACK
    extern JackSeq jack;
    #endif//WITH_JACK

    #ifdef WITH_XORG
    extern Display *xdp;
    #endif//WITH_XORG

    extern bool quit;

void
lua_init_new( lua_State * L)
{
    // push main functions to lua
    lua_pushcfunction( L, lua_midisend );
    lua_setglobal( L, "midi_send" );

    lua_pushcfunction( L, lua_exec );
    lua_setglobal( L, "exec" );

    lua_pushcfunction( L, lua_quit );
    lua_setglobal( L, "quit" );

#ifdef WITH_XORG
    // push x11 functions to lua
    lua_pushcfunction( L, lua_keypress );
    lua_setglobal( L, "keypress" );

    lua_pushcfunction( L, lua_keydown );
    lua_setglobal( L, "keydown" );

    lua_pushcfunction( L, lua_keyup );
    lua_setglobal( L, "keyup" );

    lua_pushcfunction( L, lua_buttonpress );
    lua_setglobal( L, "buttonpress" );

    lua_pushcfunction( L, lua_buttondown );
    lua_setglobal( L, "buttondown" );

    lua_pushcfunction( L, lua_buttonup );
    lua_setglobal( L, "buttonup" );

    lua_pushcfunction( L, lua_mousemove );
    lua_setglobal( L, "mousemove" );

    lua_pushcfunction( L, lua_mousepos );
    lua_setglobal( L, "mousepos" );
#endif//WITH_XORG

    return;
}

int
lua_loadscript( lua_State *L, const fs::path &script )
{
    fs::path path = getPath( script );
    if( path.empty() ) return -1;

    LOG( INFO ) << "Loading config: " << path << "\n";
    if( luaL_loadfile( L, path.c_str() ) || lua_pcall( L, 0, 0, 0 ) ){
        LOG( ERROR ) << "failure loading config file: " << lua_tostring( L, -1 ) << "\n";
        return -1;
    }
    return 0;
}

int
lua_midirecv( lua_State *L, const midi_event &event )
{
    lua_getglobal( L, "midi_recv" );
    lua_pushnumber( L, event.status );
    lua_pushnumber( L, event.data1 );
    lua_pushnumber( L, event.data2 );
    if( lua_pcall( L, 3, 0, 0 ) != 0 )
        LOG( ERROR ) << "call to function 'event_in' failed" << lua_tostring( L, -1 ) << "\n";
    return 0;
}

    /* ========================= Lua Bindings =========================== */
int
lua_midisend( lua_State *L )
{
    midi_event event;
    lua_pushnumber( L, 1 );
    lua_gettable( L, -2 );
    event.status =  static_cast<unsigned char>( luaL_checknumber( L, -1 ) );
    lua_pop( L, 1 );

    lua_pushnumber( L, 2 );
    lua_gettable( L, -2 );
    event.data1 =  static_cast<unsigned char>( luaL_checknumber( L, -1 ) );
    lua_pop( L, 1 );

    lua_pushnumber( L, 3 );
    lua_gettable( L, -2 );
    event.data2 =  static_cast<unsigned char>( luaL_checknumber( L, -1 ) );
    lua_pop( L, 1 );

    #ifdef WITH_ALSA
    if( m2i::alsa.valid )m2i::alsa.event_send( event );
    #endif

    #ifdef WITH_JACK
    if( m2i::jack.valid )m2i::jack.event_send( event );
    #endif
    return 0;
}

int
lua_exec( lua_State *L )
{
    std::string command;
    command = luaL_checkstring( L, -1 );
    LOG( INFO ) << "exec: " << command << "\n";

    FILE *in;
    char buff[512];
    if(! (in = popen( command.c_str(), "r" ))){
        return 1;
    }
    while( fgets(buff, sizeof(buff), in) != nullptr ){
        LOG( INFO ) << buff << "\n";
    }
    pclose( in );
    return 0;
}

int
lua_quit( lua_State *L )
{
    (void)L; //shutup unused variable;
    m2i::quit = true;
    return 0;
}

#ifdef WITH_XORG
    /* ===================== X11 Lua Bindings =========================== */
int
lua_keypress( lua_State *L )
{
    auto keysym = static_cast<KeySym>( luaL_checknumber( L, 1 ) );
    KeyCode keycode = XKeysymToKeycode( m2i::xdp, keysym );
    XTestFakeKeyEvent( m2i::xdp, keycode, 1, CurrentTime );
    XTestFakeKeyEvent( m2i::xdp, keycode, 0, CurrentTime );
    LOG(INFO) << "keypress: " << XKeysymToString( keysym ) << "\n";
    return 0;
}

int
lua_keydown( lua_State *L )
{
    auto keysym = static_cast<KeySym>( luaL_checknumber( L, 1 ) );
    KeyCode keycode = XKeysymToKeycode( m2i::xdp, keysym );
    XTestFakeKeyEvent( m2i::xdp, keycode, 1, CurrentTime );
    LOG(INFO) << "keydown: " << XKeysymToString( keysym ) << "\n";
    return 0;
}

int
lua_keyup( lua_State *L )
{
    auto keysym = static_cast<KeySym>( luaL_checknumber( L, 1 ) );
    KeyCode keycode = XKeysymToKeycode( m2i::xdp, keysym );
    XTestFakeKeyEvent( m2i::xdp, keycode, 0, CurrentTime );
    LOG(INFO) << "keyup: " << XKeysymToString( keysym ) << "\n";
    return 0;
}

int
lua_buttonpress( lua_State *L )
{
    auto button = static_cast<uint32_t>( luaL_checknumber( L, 1 ) );
    XTestFakeButtonEvent( m2i::xdp, button, 1, CurrentTime );
    XTestFakeButtonEvent( m2i::xdp, button, 0, CurrentTime );
    LOG(INFO) << "buttonpress: " << button << "\n";
    return 0;
}

int
lua_buttondown( lua_State *L )
{
    auto button = static_cast<uint32_t>( luaL_checknumber( L, 1 ) );
    XTestFakeButtonEvent( m2i::xdp, button, 1, CurrentTime );
    LOG(INFO) << "buttondown: " << button << "\n";
    return 0;
}

int
lua_buttonup( lua_State *L )
{
    auto button = static_cast<uint32_t>( luaL_checknumber( L, 1 ) );
    XTestFakeButtonEvent( m2i::xdp, button, 0, CurrentTime );
    LOG(INFO) << "buttonup: " << button << "\n";
    return 0;
}

int
lua_mousemove( lua_State *L )
{
    auto x = static_cast<int32_t>( luaL_checknumber( L, 1 ) );
    auto y = static_cast<int32_t>( luaL_checknumber( L, 2 ) );
    XTestFakeRelativeMotionEvent( m2i::xdp, x, y, CurrentTime );
    LOG(INFO) << "mousemove: " << x << "," << y << "\n";
    return 0;
}

int
lua_mousepos( lua_State *L )
{
    auto x = static_cast<int32_t>( luaL_checknumber( L, 1 ) );
    auto y = static_cast<int32_t>( luaL_checknumber( L, 2 ) );;
    XTestFakeMotionEvent( m2i::xdp, -1, x, y, CurrentTime );
    LOG(INFO) << "mousewarp: " << x << "," << y << "\n";
    return 0;
}

int
lua_detectwindow( lua_State *L )
{
    std::string windowname = m2i::XDetectWindow( m2i::xdp );
    LOG(INFO) << "detectwindow: " << windowname << "\n";

    lua_pushstring( L ,  windowname.c_str() ); 
    lua_setglobal( L, "WM_CLASS" ); 
    return 0;
}
#endif//WITHXORG

}// end namespace m2i