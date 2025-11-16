#if !defined(GAME_API)
	#define GAME_API /* NOTHING */

	#if defined(WIN32) || defined(WIN64)
		#undef GAME_API
		#if defined(game_EXPORTS)
			#define GAME_API __declspec(dllexport)
		#else
			#define GAME_API __declspec(dllimport)
		#endif
	#endif // defined(WIN32) || defined(WIN64)

#endif // !defined(GAME_API)

