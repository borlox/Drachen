#include "pch.h"
#include "DataPaths.h"
#include "Game.h"
#include "MainMenu.h"
#include "Win.h"
#include "Loose.h"
#include "LevelPicker.h"
#include "TextDisplay.h"
#include "Utility.h"
#include "ResourceManager.h"
#include "Theme.h"
#include "Log.h"

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace fs = boost::filesystem;

// global resource manager variables
ResourceManager<sf::Image> gImageManager;

Theme gTheme;

GlobalStatus gStatus;

void HandleException(boost::exception& ex);

int main(int argc, char **argv)
{
#ifdef _DEBUG
	Log::Logger::Instance().SetLogLevel(Log::Logger::Debug);
#endif

	LOG(Msg, "Drachen startup");

	std::ofstream fcerr("cerr.log");
	std::cerr.rdbuf(fcerr.rdbuf());

	std::ofstream fcout("cout.log");
	std::cout.rdbuf(fcout.rdbuf());


	try {
		auto fn = GetStatusFile();
		if (fs::exists(fn)) {
			LOG(Msg, "Save file " << fn.string() << " exists, loading global status");
			gStatus.LoadFromFile(fn.string());
		}
		else {
			LOG(Msg, "Save file " << fn.string() << " does not exist, resetting global status.");
			gStatus.Reset();
		}

		gStatus.settings.useShader = true;

		RenderWindow window(sf::VideoMode(800, 600, 32), "Drachen");
		window.SetFramerateLimit(100);

		gTheme.LoadTheme("default");

		MainMenu mainMenu(window);
		Win winState(window);
		Loose looseState(window);
		Game game(window, gStatus);
		LevelPicker levelPicker(window);
		TextDisplay textDisplay(window);

		State state = ST_MAIN_MENU;
		bool newState = true;

		while (window.IsOpened()) {
			if (newState) {
				LOG(Msg, "Switched state to " << state);
			}

			switch (state) {
			case ST_MAIN_MENU:
				if (newState) {
					mainMenu.Reset();
					newState = false;
				}
				mainMenu.Run();
				if (!mainMenu.IsRunning()) {
					state = mainMenu.GetNextState();
					newState = true;
				}
				break;

			case ST_GAME:
				if (newState) {
					game.Reset();
					newState = false;
				}
				game.Run();
				if (!game.IsRunning()) {
					state = game.GetNextState();
					newState = true;
				}
				break;

			case ST_WIN:
				if (newState) {
					winState.Reset();
					newState = false;
				}
				winState.Run();
				if (!winState.IsRunning()) {
					state = winState.GetNextState();
					newState = true;
				}
				break;

			case ST_LOOSE:
				if (newState) {
					looseState.Reset();
					newState = false;
				}
				looseState.Run();
				if (!looseState.IsRunning()) {
					state = looseState.GetNextState();
					newState = true;
				}
				break;

			case ST_LEVEL_PICKER:
				if (newState) {
					levelPicker.Reset();
					newState = false;
				}
				levelPicker.Run();
				if (!levelPicker.IsRunning()) {
					state = levelPicker.GetNextState();
					newState = true;
				}
				break;

			case ST_TEXT_DISPLAY:
				if (newState) {
					textDisplay.Reset();
					newState = false;
				}
				textDisplay.Run();
				if (!textDisplay.IsRunning()) {
					state = textDisplay.GetNextState();
					newState = true;
				}
				break;

			case ST_OPTIONS_MENU:
			case ST_QUIT:
				window.Close();
				break;
			}
		}

		LOG(Msg, "Window closed, saving global status.");
		gStatus.WriteToFile("drachen.st");
	}
	catch (std::runtime_error err) {
		LOG(Crit, "runtime_error: " << err.what());
		std::ofstream out("crash.log");
		out << err.what() << "\n";
	}
	catch (boost::exception& ex) {
		LOG(Crit, "GameError, saving info to crash.log");
		HandleException(ex);
	}

	return 0;
}

void HandleException(boost::exception& ex)
{
	using boost::get_error_info;

	std::stringstream msg;

	msg << "Fatal error detected";

	if (std::string const *desc = get_error_info<ErrorInfo::Desc>(ex)) {
		msg << ": " << *desc << "\n";
	}
	else {
		msg << "\n";
	}

	if (std::string const *note = get_error_info<ErrorInfo::Note>(ex)) {
		msg << *note << "\n";
	}

	if (get_error_info<ErrorInfo::Loading>(ex)) {
		if (std::string const* fileName = get_error_info<boost::errinfo_file_name>(ex)) {
			msg << "Loading '" << *fileName << "'\n";
		}
		else {
			msg << "While loading an unknown file\n";
		}
	}

	msg << "\nFull diagnostic information:\n";
	msg << diagnostic_information(ex);

	std::ofstream out("crash.log");
	out << msg.str();

#ifdef WIN32
	MessageBoxA(0, msg.str().c_str(), "Error", 0);
#endif
}
