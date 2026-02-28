#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <steam/steam_api.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include <cctype>

// TODO network structures and settings
struct Network_Settings {
    float tick_rate = 1.0f / 128.0f;
};

SteamNetworkingMicroseconds g_logTimeZero;

static void DebugOutput( ESteamNetworkingSocketsDebugOutputType eType, const char *pszMsg ) {
	SteamNetworkingMicroseconds time = SteamNetworkingUtils()->GetLocalTimestamp() - g_logTimeZero;
	printf( "%10.6f %s\n", time*1e-6, pszMsg );
	fflush(stdout);
	if ( eType == k_ESteamNetworkingSocketsDebugOutputType_Bug )
	{
		fflush(stdout);
		fflush(stderr);
	}
}

static void FatalError( const char *fmt, ... ) {
	char text[ 2048 ];
	va_list ap;
	va_start( ap, fmt );
	vsprintf( text, fmt, ap );
	va_end(ap);
	char *nl = strchr( text, '\0' ) - 1;
	if ( nl >= text && *nl == '\n' )
		*nl = '\0';
	DebugOutput( k_ESteamNetworkingSocketsDebugOutputType_Bug, text );
}

static void Printf( const char *fmt, ... ) {
	char text[ 2048 ];
	va_list ap;
	va_start( ap, fmt );
	vsprintf( text, fmt, ap );
	va_end(ap);
	char *nl = strchr( text, '\0' ) - 1;
	if ( nl >= text && *nl == '\n' )
		*nl = '\0';
	DebugOutput( k_ESteamNetworkingSocketsDebugOutputType_Msg, text );
}

static void InitSteamDatagramConnectionSockets() {
	#ifdef STEAMNETWORKINGSOCKETS_OPENSOURCE
		SteamDatagramErrMsg errMsg;
		if ( !GameNetworkingSockets_Init( nullptr, errMsg ) )
			FatalError( "GameNetworkingSockets_Init failed.  %s", errMsg );
	#else
		SteamDatagram_SetAppID( 570 ); // Just set something, doesn't matter what
		SteamDatagram_SetUniverse( false, k_EUniverseDev );

		SteamDatagramErrMsg errMsg;
		if ( !SteamDatagramClient_Init( errMsg ) )
			FatalError( "SteamDatagramClient_Init failed.  %s", errMsg );

		// Disable authentication when running with Steam, for this
		// example, since we're not a real app.
		//
		// Authentication is disabled automatically in the open-source
		// version since we don't have a trusted third party to issue
		// certs.
		SteamNetworkingUtils()->SetGlobalConfigValueInt32( k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1 );
	#endif

	g_logTimeZero = SteamNetworkingUtils()->GetLocalTimestamp();

	SteamNetworkingUtils()->SetDebugOutputFunction( k_ESteamNetworkingSocketsDebugOutputType_Msg, DebugOutput );
}

static void ShutdownSteamDatagramConnectionSockets()
{
	// Give connections time to finish up.  This is an application layer protocol
	// here, it's not TCP.  Note that if you have an application and you need to be
	// more sure about cleanup, you won't be able to do this.  You will need to send
	// a message and then either wait for the peer to close the connection, or
	// you can pool the connection to see if any reliable data is pending.
	std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

	#ifdef STEAMNETWORKINGSOCKETS_OPENSOURCE
		GameNetworkingSockets_Kill();
	#else
		SteamDatagramClient_Kill();
	#endif
}


