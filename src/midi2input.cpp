#include <iostream>
#include <cstdio>
#include <iomanip>
#include <fstream>
#include <queue>
#include <sstream>
#include <cstring>

#include <unistd.h>

extern "C" {
    #include <lua5.2/lua.h>
    #include <lua5.2/lauxlib.h>
    #include <lua5.2/lualib.h>
}

#ifdef WITH_JACK
    #include <jack/jack.h>
    #include <jack/midiport.h>
#endif

#ifdef WITH_ALSA
    #include <alsa/asoundlib.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>

#include "argh.h"

#define INFO "[INFO]"
#define WARN "[WARN]"
#define ERROR "[ERROR]"
#define FATAL "[FATAL]"
#define LOG( TYPE ) std::cout << "\n" << TYPE

lua_State *L;
Display* xdp;

#ifdef WITH_ALSA
    snd_seq_t *handle;
#endif

#ifdef WITH_JACK
    jack_client_t *client;
    jack_port_t *input_port;
    jack_port_t *output_port;
#endif

const char *helptext =
"USAGE: ./midi2input [options]"
"GENERAL OPTIONS:"
"   -h  --help      Print usage and exit"
"   -v  --verbose   Output more information"
"   -c  --config    SpeSpecify config file, default = ~/.config/midi2input.lua";


static int
lua_keypress( lua_State *L )
{
    KeySym keysym = luaL_checknumber( L, 1 );
    KeyCode keycode = XKeysymToKeycode( xdp, keysym );
    XTestFakeKeyEvent( xdp, keycode, 1, CurrentTime );
    XTestFakeKeyEvent( xdp, keycode, 0, CurrentTime );
    LOG(INFO) << "keypress: " << XKeysymToString( keysym );
    return 0;
}

static int
lua_keydown( lua_State *L )
{
    KeySym keysym = luaL_checknumber( L, 1 );
    KeyCode keycode = XKeysymToKeycode( xdp, keysym );
    XTestFakeKeyEvent( xdp, keycode, 1, CurrentTime );
    LOG(INFO) << "keydown: " << XKeysymToString( keysym );
    return 0;
}

static int
lua_keyup( lua_State *L )
{
    KeySym keysym = luaL_checknumber( L, 1 );
    KeyCode keycode = XKeysymToKeycode( xdp, keysym );
    XTestFakeKeyEvent( xdp, keycode, 0, CurrentTime );
    LOG(INFO) << "keyup: " << XKeysymToString( keysym );
    return 0;
}

static int
lua_buttonpress( lua_State *L )
{
    unsigned int button = luaL_checknumber( L, 1 );
    XTestFakeButtonEvent( xdp, button, 1, CurrentTime );
    XTestFakeButtonEvent( xdp, button, 0, CurrentTime );
    LOG(INFO) << "buttonpress: " << button;
    return 0;
}

static int
lua_buttondown( lua_State *L )
{
    unsigned int button = luaL_checknumber( L, 1 );
    XTestFakeButtonEvent( xdp, button, 1, CurrentTime );
    LOG(INFO) << "buttondown: " << button;
    return 0;
}

static int
lua_buttonup( lua_State *L )
{
    unsigned int button = luaL_checknumber( L, 1 );
    XTestFakeButtonEvent( xdp, button, 0, CurrentTime );
    LOG(INFO) << "buttonup: " << button;
    return 0;
}

static int
lua_mousemove( lua_State *L )
{
    int x = luaL_checknumber( L, 1 );
    int y = luaL_checknumber( L, 2 );
    XTestFakeRelativeMotionEvent( xdp, x, y, CurrentTime );
    LOG(INFO) << "mousemove: " << x << "," << y;
    return 0;
}

static int
lua_mousepos( lua_State *L )
{
    int x = luaL_checknumber( L, 1 );
    int y = luaL_checknumber( L, 2 );
    XTestFakeMotionEvent( xdp, -1, x, y, CurrentTime );
    LOG(INFO) << "mousewarp: " << x << "," << y;
    return 0;
}

static int
lua_midi_send( lua_State *L )
{
#ifdef WITH_ALSA
    //example of how to send midi data to the port.
        snd_seq_event_t ev;
        snd_seq_ev_clear( &ev );
        snd_seq_ev_set_source( &ev, 1 );
        snd_seq_ev_set_subs( &ev );
        snd_seq_ev_set_direct( &ev );

        // set event type, data, so on..
        snd_seq_event_output( handle, &ev);
        snd_seq_drain_output( handle );  // if necessary
#endif

#ifdef WITH_JACK
    void *port_buf = jack_port_get_buffer( output_port, 0 );
    if(! port_buf ){
        LOG( ERROR ) << "Cannot send events with no connected ports";
        return 1;
    }
    jack_midi_clear_buffer( port_buf );

    jack_midi_data_t *event = jack_midi_event_reserve( port_buf, 0, 3 );
    for( int i = 0; i < 3; ++i ){
        lua_pushnumber( L, i + 1 );
        lua_gettable( L, -2 );
        event[ i ] =  luaL_checknumber( L, -1 );
        lua_pop( L, 1 );
    }

    LOG( INFO ) << "midi-send: "
       << std::hex << std::setfill( '0' ) << std::uppercase
       << "0x" << std::setw( 2 ) << event[ 0 ] << ", "
       << "0x" << std::setw( 2 ) << event[ 1 ] << ", "
       << std::dec << std::setfill( ' ' ) << std::setw( 3 ) << event[ 2 ];

    jack_midi_event_write( port_buf, 0, event, 3 );
#endif
    return 0;
}

static int
lua_exec( lua_State *L )
{
    std::string command;
    command = luaL_checkstring( L, -1 );
    LOG( INFO ) << "exec: " << command;

    FILE *in;
    char buff[512];
    if(! (in = popen( command.c_str(), "r" ))){
        return 1;
    }
    while( fgets(buff, sizeof(buff), in) != NULL ){
        LOG( INFO ) << buff;
    }
    pclose( in );
    return 0;
}

bool
load_config( std::string name )
{
    // load configuration from a priority list of locations
    // * specified from the command line
    // * configuration folder $HOME/.config/
    // * home directory $HOME/
    std::string filepath;
    std::queue<std::string> paths;

    if(! name.empty() ) paths.push( name );

    // configuration folder ~/.config/
    filepath = std::string( getenv( "HOME" ) ) + "/.config/midi2input.lua";
    paths.push( filepath );

    // configuration folder ~/.config/
    filepath = std::string( getenv( "HOME" ) ) + "/.midi2input.lua";
    paths.push( filepath );

    std::ifstream tFile;
    while(! paths.empty() ){
        tFile.open( paths.front().c_str(), std::ifstream::in );
        if( tFile.is_open() ){
            tFile.close();
            break;
        }
        paths.pop();
    }
    if( paths.empty() ) return false;

    if( luaL_loadfile( L, paths.front().c_str() ) || lua_pcall( L, 0, 0, 0 ) ){
        LOG( ERROR ) << "cannot run configuration file: " << lua_tostring( L, -1 );
        return false;
    }
    LOG( INFO ) << "Using: " << paths.front();
    return true;
}

int XErrorCatcher( Display *disp, XErrorEvent *xe )
{
    printf( "Something had happened, bruh.\n" );
    return 0;
}

Window
XGetTopLevelParent( Display *xdp, Window w )
{
        LOG( ERROR ) << "Failed XInternAtom for WM_CLASS";
    Atom property = XInternAtom( xdp, "WM_CLASS", False );
    if( property == None ){
    }

    Atom actual_type_return;
    int actual_format_return = 0;
    unsigned long nitems_return = 0L;
    unsigned long bytes_after_return = 0L;
    unsigned char *prop_return = NULL;

    if( XGetWindowProperty( xdp, w, property, 0L, 1024L, False, XA_STRING,
                &actual_type_return,
                &actual_format_return,
                &nitems_return,
                &bytes_after_return,
                &prop_return ) != Success ){
        LOG( ERROR ) << "Failed XGetWindowProperty for WM_CLASS";
    }
    if( (actual_type_return == XA_STRING) && (actual_format_return == 8) ){
        LOG( INFO ) << "WM_CLASS: " << prop_return;

        lua_pushstring( L , reinterpret_cast<const char *>(prop_return) );
        lua_setglobal( L, "wm_class" );

        XFree( prop_return );
        return w;
    }

    // no WM_CLASS property found, so lets get the parent window
    Window root_return;
    Window parent_return;
    Window *children_return = NULL;
    unsigned int nchildren_return = 0;

    if( XQueryTree( xdp, w,
                &root_return,
                &parent_return,
                &children_return,
                &nchildren_return ) ){
        if( children_return != NULL ) XFree( children_return );
        if( parent_return != DefaultRootWindow( xdp ) )
            w = XGetTopLevelParent( xdp, parent_return );
    }
    else {
        LOG( ERROR ) << "Failed XQueryTree";
    }

    return w;
}

#ifdef WITH_JACK
int
process( jack_nframes_t nframes, void *arg )
{
    //FIXME this happens for every event, rather than intermittantly
    //which causes Xorg to take up 70% of my cpu when i'm pulling
    //midi events from a dsp envelope tracker and feeding it back.
    //i would rather it not do this.
    //detect window
    static Window w_current;
    Window w;
    int revert_to;

    XGetInputFocus( xdp, &w, &revert_to );
    if( w != None && w != w_current ){
        w_current = w;
        XGetTopLevelParent( xdp, w );
    }

    // TODO use inotify to monitor the state of the configuration file and
    // reload if it has changed

    //process midi events
    void* port_buf = jack_port_get_buffer( input_port, nframes );
    jack_midi_event_t in_event;
    static jack_midi_event_t last_event;
    static unsigned char last_buffer[4];
    last_event.buffer = last_buffer;

    jack_nframes_t event_count = jack_midi_get_event_count( port_buf );

    if( event_count > 0 ){
        LOG( INFO ) << event_count << " Events in queue.";
        for(uint32_t i = 0; i < event_count; i++ ){
            jack_midi_event_get( &in_event, port_buf, i );
            jack_midi_data_t *event = in_event.buffer;

            LOG( INFO ) << "midi-recv: "
               << std::hex << std::setfill( '0' ) << std::uppercase
               << "0x" << std::setw( 2 ) << event[ 0 ] << ", "
               << "0x" << std::setw( 2 ) << event[ 1 ] << ", "
               << std::dec << std::setfill( ' ' ) << std::setw( 3 ) << event[ 2 ];
            lua_getglobal( L, "midi_recv" );
            lua_pushnumber( L, event[ 0 ] );
            lua_pushnumber( L, event[ 1 ] );
            lua_pushnumber( L, event[ 2 ] );
            if( lua_pcall( L, 3, 0, 0 ) != 0 )
                LOG( ERROR ) << "call to function 'event_in' failed"
                    << lua_tostring( L, -1 );
        }
    }
    return 0;
}

void
jack_shutdown( void *arg )
{
    exit( 1 );
}

#endif

int
main( int argc, const char **argv )
{
    argh::parser cmdl( argc, argv );

    // Options Parsing
    // ===============
    // setup logging level.
    if( cmdl[{ "-v", "--verbose" }] )
    //if( options[ QUIET ] )
    //    LOG::SetDefaultLoggerLevel( LOG::CHECK );

    if( cmdl[{"-h", "--help"}] ){
        LOG(INFO) << helptext;
        exit( 0 );
    }

    // --config
    LOG( INFO ) << "Parsing cmd line options";
    std::string luaScript;
    if( cmdl[{"-c", "--config"}] )
    {
        luaScript = "";
    } else luaScript = "~/.config/midi2input.lua";

    /* X11 */
    LOG( INFO ) << "Getting X11 Display";
    if(! (xdp = XOpenDisplay( getenv( "DISPLAY" ) )) ){
        LOG( FATAL ) << "Unable to open X display";
        exit( -1 );
    }
    // set XErrorHandler
    XSetErrorHandler( XErrorCatcher );

    /* Lua */
    LOG( INFO ) << "Initialising Lua";
    L = luaL_newstate();
    luaL_openlibs( L );

    LOG( INFO ) << "Lua: pushing c functions";
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

    lua_pushcfunction( L, lua_midi_send );
    lua_setglobal( L, "midi_send" );

    lua_pushcfunction( L, lua_exec );
    lua_setglobal( L, "exec" );

    LOG( INFO ) << "Lua: Loading configuration file";
    if(! load_config( luaScript.c_str() ) ){
        LOG( FATAL ) << "Unable to open configuration file, expecting ~/.config/midi2input.lua, or -c switch.";
        exit( -1 );

    }
#ifdef WITH_ALSA
    int err;
    err = snd_seq_open( &handle, "default", SND_SEQ_OPEN_INPUT, 0 );
    if( err < 0 ) LOG( FATAL ) << "Problem creating midi sequencer ports";

    snd_seq_set_client_name( handle, "midi2input" );

    snd_seq_create_simple_port( handle, "in",
            SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC );

    snd_seq_create_simple_port( handle, "out",
            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC );
#endif


#ifdef WITH_JACK
    /* Jack */
    LOG( INFO ) << "Initialising Jack";
    if( (client = jack_client_open( "midi2input", JackNullOption, NULL )) == 0 ){
        LOG( FATAL ) << "jack server not running?";
        exit( -1 );

    }

    LOG( INFO ) << "Jack: setting callbacks";
    jack_set_process_callback( client, process, 0 );
    jack_on_shutdown( client, jack_shutdown, 0 );

    LOG( INFO ) << "Jack: registering ports";
    input_port = jack_port_register( client, "midi_in",
            JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0 );
    output_port = jack_port_register( client, "midi_out",
            JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0 );

    LOG( INFO ) << "Jack: Activating client";
    if( jack_activate( client ) ){
        LOG( FATAL ) << "cannot activate client";
        exit( -1 );

    }
#endif

    /* Lua: get port connection configuration */
    LOG( INFO ) << "Lua: Getting autoconnection setting";
    std::string autoconnect;
    lua_getglobal( L, "autoconnect" );
    if(! lua_isnil( L, -1 ) ){
        if( lua_isboolean( L, -1 ) ){
            if(! lua_toboolean( L, -1 ) ){
                autoconnect = "None";
            }
        }
        else if( lua_isnumber( L, -1 ) ){
            LOG( WARN ) << "parse error for 'autoconnect'";
        }
        else if( lua_isstring( L, -1 ) ){
            autoconnect = lua_tostring( L, -1);
        } else {
            LOG( WARN ) << "parse error for 'autoconnect'\n";
        }
    }
    if( autoconnect.empty() ){
        autoconnect = "All";
    }
    LOG( INFO ) << "Lua: autoconnect = " << autoconnect;
    lua_pop( L, 1 );

#ifdef WITH_JACK
    /* Jack: get list of output ports */
    LOG( INFO ) << "Jack: Looking up output ports";
    int i = 0;
    const char ** portnames = jack_get_ports( client, ".midi.", NULL, JackPortIsOutput );
    if(! portnames ){
        LOG( FATAL ) << "ERROR: no ports available";
        exit( -1 );

    }

    while( *(portnames + i) ){
        LOG( INFO ) << "Jack: Found: " << *(portnames+i);
        i++;
    }

    /* Jack: Connect to output ports */
    LOG( INFO ) << "Jack: Connecting to output ports";
    std::stringstream message;
    std::string portname;
    if( autoconnect.compare( "None" ) ){
        if(! autoconnect.compare( "All" ) ){
            if(! portnames ){
                LOG( FATAL ) << "No ports to connect to";
                exit( -1 );
            }
            i = 0;
            while( *(portnames + i) ){
                portname = *(portnames + i);
                if(! portname.compare("midi2input:midi_out") ){
                    ++i;
                    continue;
                }
                message.str( std::string() );
                message << "Connecting to: " << portname;
                if( jack_connect( client, portname.c_str(), "midi2input:midi_in" ) ){
                    message << " - FAILED";
                } else {
                    message << " - SUCCESS";
                }
                LOG( INFO ) << message.str();
                ++i;
            }
        } else {
            message << "Connecting to: " << autoconnect;
            if( jack_connect( client, autoconnect.c_str(), "midi2input:midi_in" ) ){
                    message << " - FAILED\n";
            } else {
                    message << " - SUCCESS\n";
            }
            LOG(INFO) << message.str();
        }
    }
    jack_free( portnames );

    /* Jack: get list of input ports */
    LOG( INFO ) << "Jack: Looking up input ports";
    i = 0;
    portnames = jack_get_ports( client, ".midi.", NULL, JackPortIsInput );
    if(! portnames ){
        LOG( FATAL ) << "ERROR: no ports available";
        exit( -1 );
    }
    while( *(portnames + i) ){
        LOG( INFO ) << "Jack: Found: " << *(portnames+i);
        i++;
    }

    /* Jack: Connect to input ports */
    LOG( INFO ) << "Jack: Connecting to input ports";
    if(! portnames ){
        LOG( FATAL ) << "No ports to connect to";
        exit( -1 );
    }
    i = 0;
    while( *(portnames + i) ){
        portname = *(portnames + i);
        if(! portname.compare("midi2input:midi_in") ){
            ++i;
            continue;
        }
        message.str( std::string() );
        message << "Connecting to: " << *(portnames + i);
        if( jack_connect( client, "midi2input:midi_out", *(portnames + i) ) ){
            message << " - FAILED";
        } else {
            message << " - SUCCESS";
        }
        LOG( INFO ) << message.str();
        i++;
    }
    jack_free( portnames );
#endif

    // Run initialisation lua function if output ports are set
#ifdef WITH_JACK
    void *port_buf = jack_port_get_buffer( output_port, 0 );
    if(! port_buf ){
        LOG( ERROR ) << "Cannot send events with no connected ports";
        return 1;
    }
    else
#endif
    {
        LOG( INFO ) << "Running Initialisation function";
        lua_getglobal( L, "initialise" );
        lua_call( L, 0, 0);
    }

    /* main loop */
    LOG( INFO ) << "Main: Entering sleep, waiting for jack events";
    while( 1 ) sleep( 1 );

    // FIXME this code is never run due to the infinate loop above. make a way
    // to exit the loop without CTRL+C
    //
#ifdef WITH_JACK
    jack_client_close( client );
#endif
    lua_close( L );
    exit( 0 );
}


