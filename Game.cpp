#include "pch.h"
#include "Game.h"
#include "Utility.h"
#include "Tower.h"
#include "DataPaths.h"
#include "TowerSettings.h"
#include "ResourceManager.h"

#include "json_spirit/json_spirit.h"

namespace fs = boost::filesystem;
namespace js = json_spirit;

static float SPAWN_TIME = .5f;

#pragma warning (disable: 4355)
Game::Game(RenderWindow& win, GlobalStatus& gs)
: window(win), globalStatus(gs), userInterface(this, window, globalStatus, gameStatus, &map), running(true)
{ }

void Game::Reset()
{
	enemies.clear();
	towers.clear();
	projectiles.clear();

	gameStatus.Reset(globalStatus);
	LoadLevel(globalStatus.level);
	LoadEnemySettings();
	LoadFromFile(map, levelInfo.map);

	gTheme.LoadTheme(levelInfo.theme);
	userInterface.Reset(levelInfo);

	// reset countdown and spawn timer here for the first wave
	gameStatus.spawnTimer.Reset();
	gameStatus.countdownTimer.Reset();

	running = true;
}

// Compare towers by their y position, to ensure lower towers (= higher y pos) are drawn
// later, so the overlap is displayed correctly.
static bool CompTowerY(const Tower& a, const Tower& b)
{
	return a.GetPosition().y < b.GetPosition().y;
}

static bool ShouldRemoveEnemy(const std::shared_ptr<Enemy>& e)
{
	return e->IsIrrelevant();
}

// Main function of the game class, this gets called every frame.
void Game::Run()
{
	// Handle all SFML events
	Event event;
	while (window.GetEvent(event)) {
		// Handle default stuff like window closed etc.
		if (DefaultHandleEvent(window, event))
			continue;

		if (userInterface.HandleEvent(event))
			continue;

		// some debug keys
		if (event.Type == Event::KeyReleased) {
			switch (event.Key.Code) {
			case Key::G:
				SpawnEnemy(0);
				break;
			case Key::F2:
				map.ToggleOverlay();
				break;
			case Key::F3:
				map.DebugToggleTowersAnywhere();
				break;
			}
		}
	}

	float elapsed = window.GetFrameTime();

	// Update the wave state
	UpdateWave();

	// Go through all enemies, projectiles and towers and update them
	for (auto it = enemies.begin(); it != enemies.end(); ++it) {
		std::shared_ptr<Enemy> e = *it;
		e->Update(elapsed);
		// If an enemy reached the target area and did not strike yet,
		// let them strike and loose a life. Poor player )-:
		if (e->IsAtTarget() && !e->DidStrike()) {
			e->Strike();
			LooseLife();
		}
	}
	for (auto it = projectiles.begin(); it != projectiles.end(); ++it)
		it->Update(elapsed);
	for (auto it = towers.begin(); it != towers.end(); ++it)
		it->Update(elapsed);

	// grant money for dead enemies
	boost::for_each(enemies, [&](const std::shared_ptr<Enemy>& e) {
		if (e->IsDead())
			gameStatus.money += gStatus.moneyPerEnemy * e->GetMoneyFactor();
	});

	// Remove all the things no longer needed
	projectiles.erase(boost::remove_if(projectiles, boost::bind(&Projectile::DidHit, _1)), projectiles.end());
	enemies.erase(boost::remove_if(enemies, ShouldRemoveEnemy), enemies.end());

	userInterface.Update();

	// And draw all the stuff
	window.Clear();
	map.Draw(window);
	for (auto it = towers.begin(); it != towers.end(); ++it) {
		window.Draw(*it);
	}
	for (auto it = enemies.begin(); it != enemies.end(); ++it) {
		(*it)->DrawHpBar(window);
		window.Draw(*(*it));
	}
	
	for (auto it = projectiles.begin(); it != projectiles.end(); ++it)
		window.Draw(*it);

	// Draw the user interface at last, so it does not get hidden by any objects
	userInterface.Draw();

	window.Display();
}

void Game::UpdateWave()
{
	if (gameStatus.currentWave >= gameStatus.waves.size()) {
		// if we finished the last wave, the game has ended
		if (enemies.size() == 0)
			running = false;

		// return even if there are still enemies, there is nothing wave related to handle 
		// anymore (and currentWave points to the wave after the end of gameStatus.waves (-; )
		return;
	}

	GameStatus::Wave& currentWave = gameStatus.waves[gameStatus.currentWave];

	switch (gameStatus.waveState) {
	case GameStatus::InCountdown:
		// We are in the InCountdown state. If the countdown has elapsed, begin to spawn
		// the enemies by proceeding to the InSpawn state;
		if (gameStatus.countdownTimer.GetElapsedTime() > currentWave.countdown) {
			gameStatus.waveState = GameStatus::InSpawn;
			gameStatus.waveTimer.Reset();
		}
		break;
	case GameStatus::InSpawn:
		// In the spawn state see if the spawn timer has elapsed and then spawn an enemy.
		// If all enemies for this wave are spawned proceed to the InWave state
		if (gameStatus.spawnTimer.GetElapsedTime() > SPAWN_TIME) {
			if (currentWave.enemies.size() > 0) {
				SpawnEnemy(currentWave.enemies.front());
				currentWave.enemies.pop();
				gameStatus.spawnTimer.Reset();
			}
			else {
				gameStatus.waveState = GameStatus::InWave;
			}
		}
		break;
	case GameStatus::InWave:
		// In the InWave state wait untill all enemies are killed or the maximal time for the wave has 
		// elapsed, then proceed to the next wave.
		// Reset both countdownTimer and spawnTimer here, so the first spawn will happen immediatly when
		// the wave countdown finished (as long as countdown > SPAWN_TIME).
		if (enemies.size() == 0 || (currentWave.maxTime != 0 && gameStatus.waveTimer.GetElapsedTime() > currentWave.maxTime)) {
			gameStatus.currentWave++;
			gameStatus.waveState = GameStatus::InCountdown;
			gameStatus.countdownTimer.Reset();
			gameStatus.spawnTimer.Reset();
		}
		break;
	}
}

void Game::LooseLife()
{
	gameStatus.lives--;
	if (gameStatus.lives == 0) {
		running = false;
	}
}

bool Game::IsRunning()
{
	return running;
}

State Game::GetNextState()
{
	return ST_MAIN_MENU;
}

void Game::LoadLevel(const std::string& level)
{
	fs::path levelPath = GetLevelPath(level);
	fs::path levelDef = levelPath / LevelDefinitionFile;

	std::ifstream in(levelDef.string());
	js::mValue rootValue;
	try {
		js::read_or_throw(in, rootValue);
	}
	catch (js::Error_position err) {
		throw GameError() << ErrorInfo::Desc("Invalid json file") << ErrorInfo::Note(err.reason_) << boost::errinfo_at_line(err.line_) << boost::errinfo_file_name(levelDef.string());
	}

	if (rootValue.type() != js::obj_type)
		throw GameError() << ErrorInfo::Desc("Root value is not an object");

	js::mObject rootObj = rootValue.get_obj();

	try {
		levelInfo.name = rootObj["name"].get_str();
		levelInfo.map = rootObj["map"].get_str();
		levelInfo.theme = rootObj["theme"].get_str();

		js::mArray& waves = rootObj["waves"].get_array();
		for (size_t i=0; i < waves.size(); ++i) {
			js::mObject& waveDef = waves[i].get_obj();

			GameStatus::Wave wave;
			wave.countdown = waveDef["countdown"].get_int();

			js::mArray enemies = waveDef["enemies"].get_array();
			for (size_t j=0; j < enemies.size(); ++j) {
				js::mArray what = enemies[j].get_array();

				size_t type = what[0].get_int();
				size_t num = what[1].get_int();
				for (size_t k=0; k < num; ++k)
					wave.enemies.push(type);
			}

			if (waveDef.count("max-time"))
				wave.maxTime = waveDef["max-time"].get_int();
			else
				wave.maxTime = 0;

			gameStatus.waves.push_back(wave);
		}
	}
	catch (std::runtime_error err) {
		throw GameError() << ErrorInfo::Desc("Json error") << ErrorInfo::Note(err.what()) << boost::errinfo_file_name(levelDef.string());
	}
}

void Game::LoadEnemySettings()
{
	enemySettings.clear();

	fs::path themePath = GetThemePath(levelInfo.theme);
	fs::path enemyDef = themePath / EnemyDefinitionFile;

	std::ifstream in(enemyDef.string());
	js::mValue rootValue;
	try {
		js::read_or_throw(in, rootValue);
	}
	catch (js::Error_position err) {
		throw GameError() << ErrorInfo::Desc("Invalid json file") << ErrorInfo::Note(err.reason_) << boost::errinfo_at_line(err.line_) << boost::errinfo_file_name(enemyDef.string());
	}

	if (rootValue.type() != js::obj_type)
		throw GameError() << ErrorInfo::Desc("Root value is not an object") << boost::errinfo_file_name(enemyDef.string());

	js::mObject rootObj = rootValue.get_obj();

	js::mArray& enemies = rootObj["enemies"].get_array();
	enemySettings.resize(enemies.size());
	for (size_t i=0; i < enemies.size(); ++i) {
		js::mObject& def = enemies[i].get_obj();

		enemySettings[i].image     = &gImageManager.getResource((themePath / def["image"].get_str()).string());
		enemySettings[i].width     = def["width"].get_int();
		enemySettings[i].height    = def["height"].get_int();
		enemySettings[i].offset    = def["offset"].get_int();
		enemySettings[i].numFrames = def["frames"].get_int();
		enemySettings[i].frameTime = static_cast<float>(def["frame-time"].get_real());
		
		enemySettings[i].life  = def["life"].get_int();
		enemySettings[i].speed = static_cast<float>(def["speed"].get_real());
		enemySettings[i].moneyFactor = def["money-factor"].get_int();
	}
}

void Game::SpawnEnemy(size_t type)
{
	std::shared_ptr<Enemy> e(new Enemy(enemySettings[type], &map));
	e->SetPosition(map.GetSpawnPosition());
	e->SetTarget(map.GetDefaultTarget());
	enemies.push_back(e);
}

void Game::AddTower(TowerSettings* settings, Vector2f pos)
{
	gameStatus.money -= 100;

	Tower t(&enemies, &projectiles);
	t.SetImage(*settings->baseImage);
	t.SetPosition(pos);
	t.SetSize(settings->baseImage->GetWidth(), settings->baseImage->GetHeight());
	t.SetCenter(t.GetWidth() / 2.f, t.GetHeight() - t.GetWidth() / 3.f);
	towers.push_back(t);
	map.PlaceTower(map.PositionToBlock(pos));
	boost::sort(towers, CompTowerY);
}
