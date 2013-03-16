#include "pch.h"
#include "Game.h"
#include "MainMenu.h"
#include "Utility.h"
#include "ResourceManager.h"
#include "Theme.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// global resource manager variables
ResourceManager<sf::Image> gImageManager;

Theme gTheme;

GlobalStatus gStatus;

void HandleException(boost::exception& ex);

int main(int argc, char **argv)
{
	try {
		gStatus.level = "test";
		gStatus.startLives = 6;
		gStatus.startMoney = 10000;

		RenderWindow window(sf::VideoMode(800, 600, 32), "Drachen");
		window.SetFramerateLimit(100);

		gTheme.LoadTheme("default");

		MainMenu mainMenu(window);
		Game game(window, gStatus);

		State state = ST_MAIN_MENU;
		bool newState = true;

		while (window.IsOpened()) {
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

			case ST_OPTIONS_MENU:
			case ST_QUIT:
				window.Close();
				break;
			}
		}
	}
	catch (std::runtime_error err) {
		std::ofstream out("crash.log");
		out << err.what() << "\n";
	}
	catch (boost::exception& ex) {
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

	MessageBoxA(0, msg.str().c_str(), "Error", 0);
}
