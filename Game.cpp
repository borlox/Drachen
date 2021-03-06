#include "pch.h"
#include "Game.h"
#include "Utility.h"
#include "Tower.h"
#include "DataPaths.h"
#include "TowerSettings.h"
#include "ResourceManager.h"

#include "json_spirit/json_spirit.h"
#include "jsex.h"

namespace fs = boost::filesystem;
namespace js = json_spirit;

static const float SPAWN_TIME = .5f;

static const float LOADING_BAR_WIDTH = 500;

#pragma warning (disable: 4355)
Game::Game(RenderWindow& win, GlobalStatus& gs)
: window(win), globalStatus(gs), userInterface(this, window, globalStatus, gameStatus, &map), running(true), loadingScreenBar(LOADING_BAR_WIDTH, 20)
{ }

// Compare towers by their y position, to ensure lower towers (= higher y pos) are drawn
// later, so the overlap is displayed correctly.
static bool CompByY(const std::shared_ptr<sf::Drawable>& a, const std::shared_ptr<sf::Drawable>& b)
{
	return a->GetPosition().y < b->GetPosition().y;
}

void Game::Reset()
{
	postfx.LoadFromFile("data/postfx.sfx");
	postfx.SetTexture("framebuffer", nullptr);

	nightModeFx.LoadFromFile("data/night.sfx");
	nightModeFx.SetTexture("framebuffer", nullptr);

	loadingScreenBackground.SetImage(gImageManager.getResource(gTheme.GetFileName("main-menu/background")));
	loadingScreenBackground.SetPosition(0, 0);
	loadingScreenBar.SetPosition(250, 500);
	loadingScreenBar.SetColor(Color(255, 216, 0));
	UpdateLoadingScreen(0.1f);

	enemies.clear();
	towers.clear();
	projectiles.clear();

	gameStatus.Reset(globalStatus);
	UpdateLoadingScreen(0.2f);

	auto levelPath = GetLevelPath(globalStatus.runTime.level);
	auto levelFile = GetLevelFile(globalStatus.runTime.level);
	level.LoadFromFile(levelPath / levelFile);
	UpdateLoadingScreen(0.3f);

	LoadEnemySettings();
	UpdateLoadingScreen(0.4f);

	LoadFromFile(map, level.map);
	map.Reset();
	UpdateLoadingScreen(0.7f);

	fireEffects.clear();
	boost::for_each(map.GetFirePlaces(), [&](const Vector2f& pos) {
		std::shared_ptr<FireEffect> fire(new FireEffect(window.GetWidth(), window.GetHeight()));
		fire->SetFireTexture(&gImageManager.getResource("data/effects/fire.png"));
		fire->SetNoiseTexture(&gImageManager.getResource("data/effects/noise.png"));
		fire->SetAlphaTexture(&gImageManager.getResource("data/effects/alpha.png"));

		fire->SetWidth(25);
		fire->SetHeight(25);
		fire->SetPosition(pos - Vector2f(12.5, 22));

		fireEffects.emplace_back(std::move(fire));
	});
	boost::sort(fireEffects, CompByY);

	gTheme.LoadTheme(level.theme);
	userInterface.Reset(level);
	UpdateLoadingScreen(0.9f);

	// reset countdown and spawn timer here for the first wave
	gameStatus.spawnTimer.Reset();
	gameStatus.countdownTimer.Reset();
	UpdateLoadingScreen(1.f);

	running = true;
}

void Game::UpdateLoadingScreen(float pct)
{
	loadingScreenBar.SetWidth(pct * LOADING_BAR_WIDTH);

	window.Draw(loadingScreenBackground);
	window.Draw(loadingScreenBar);
	window.Display();
}

static bool IsAtPoint(const std::shared_ptr<Tower>& twr, Vector2f pt)
{
	return PointInRect(pt, twr->GetPosition() - twr->GetCenter(), static_cast<float>(twr->GetWidth()), static_cast<float>(twr->GetHeight()));
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
				SpawnEnemy(0, 0);
				break;

			case Key::Q:
				gameStatus.lives = 1;
				LooseLife();
				break;

			case Key::X:
				gStatus.settings.useShader = !gStatus.settings.useShader;
				break;

			case Key::N:
				level.nightMode = !level.nightMode;
			}
		}
		else if (event.Type == Event::MouseButtonReleased && event.MouseButton.Button == Mouse::Left) {
			Vector2f pos(static_cast<float>(event.MouseButton.X), static_cast<float>(event.MouseButton.Y));

			// make a reversed copy of the towers so that the lowest one gets selected
			std::vector<std::shared_ptr<Tower>> revTowers = towers;
			boost::reverse(revTowers);

			auto it = boost::find_if(revTowers, boost::bind(IsAtPoint, _1, pos));
			if (it != revTowers.end())
				userInterface.TowerSelected(*it);
			else
				userInterface.TowerSelected(nullptr);

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
		(*it)->Update(elapsed);
	for (auto it = towers.begin(); it != towers.end(); ++it)
		(*it)->Update(elapsed);

	if (level.nightMode) {
		boost::for_each(fireEffects, [&](const std::shared_ptr<FireEffect>& fire) {
			fire->Update(elapsed);
		});
	}

	// grant money for dead enemies
	boost::for_each(enemies, [&](const std::shared_ptr<Enemy>& e) {
		if (e->IsDead())
			gameStatus.money += gStatus.moneyPerEnemy * e->GetMoneyFactor();
	});

	// Remove all the things no longer needed
	projectiles.erase(boost::remove_if(projectiles, [](const std::unique_ptr<Projectile>& p) {
			return p->DidHit();
		}), projectiles.end());
	enemies.erase(boost::remove_if(enemies, [](const std::shared_ptr<Enemy>& e) {
			return e->IsIrrelevant();
		}), enemies.end());
	towers.erase(boost::remove_if(towers, [&](const std::shared_ptr<Tower>& t) mutable -> bool {
			if (t->IsSold()) {
				this->map.RemoveTower(t->GetPosition());
				return true;
			}
			return false;
		}), towers.end());

	userInterface.Update();

	boost::sort(enemies, CompByY);

	// And draw all the stuff
	window.Clear();
	map.Draw(window);
	userInterface.PreDraw();

	// keep towers, enemies and possibly fires sorted by their y position to correctly treat overlap
	std::vector<std::shared_ptr<Drawable>> sprites;

	if (level.nightMode) {
		sprites.reserve(towers.size() + enemies.size() + fireEffects.size());

		std::vector<std::shared_ptr<Drawable>> towersAndFires;
		boost::merge(towers, fireEffects, std::back_inserter(towersAndFires), CompByY);
		boost::merge(towersAndFires, enemies, std::back_inserter(sprites), CompByY);
	}
	else {
		sprites.reserve(towers.size() + enemies.size());
		boost::merge(towers, enemies, std::back_inserter(sprites), CompByY);
	}

	// draw the night mode shader before the towers, so they do not get too dark
	if (level.nightMode)
		window.Draw(nightModeFx);

	boost::for_each(sprites, [&](const std::shared_ptr<Drawable>& sprite) {
		std::shared_ptr<Enemy> e = std::dynamic_pointer_cast<Enemy>(sprite);
		if (e)
			e->DrawHpBar(window);
		window.Draw(*sprite);
	});
	
	for (auto it = projectiles.begin(); it != projectiles.end(); ++it)
		window.Draw(*(*it));

	if (gStatus.settings.useShader)
		window.Draw(postfx);

	// Draw the user interface at last, so it does not get hidden by any objects
	userInterface.Draw();

	window.Display();
}

void Game::UpdateWave()
{
	if (gameStatus.currentWave >= level.waves.size()) {
		// if we finished the last wave, the game has ended
		if (enemies.size() == 0)
			running = false;

		// return even if there are still enemies, there is nothing wave related to handle 
		// anymore (and currentWave points to the wave after the end of gameStatus.waves (-; )
		return;
	}

	Level::Wave& currentWave = level.waves[gameStatus.currentWave];

	switch (gameStatus.waveState) {
	case GameStatus::InCountdown:
		// We are in the InCountdown state. If the countdown has elapsed, begin to spawn
		// the enemies by proceeding to the InSpawn state;
		if (gameStatus.countdownTimer.GetElapsedTime() > currentWave.countdown) {
			gameStatus.waveState = GameStatus::InSpawn;

			// copy the enemies to spawn to a stack
			enemiesToSpawn.clear();
			enemiesToSpawn.resize(currentWave.enemies.size());
			for (size_t spawnPt = 0; spawnPt < currentWave.enemies.size(); ++spawnPt) {
				boost::for_each(currentWave.enemies[spawnPt], [&](size_t tp) {
					enemiesToSpawn[spawnPt].push(tp);
				});
			}

			gameStatus.waveTimer.Reset();
		}
		break;
	case GameStatus::InSpawn:
		// In the spawn state see if the spawn timer has elapsed and then spawn an enemy.
		// If all enemies for this wave are spawned proceed to the InWave state
		if (gameStatus.spawnTimer.GetElapsedTime() > SPAWN_TIME) {

			bool spawned = false;
			for (size_t spawnPt = 0; spawnPt < std::min(enemiesToSpawn.size(), map.GetNumSpawns()); ++spawnPt) {
				if (enemiesToSpawn[spawnPt].size() > 0) {
					SpawnEnemy(enemiesToSpawn[spawnPt].front(), spawnPt);
					enemiesToSpawn[spawnPt].pop();
					gameStatus.spawnTimer.Reset();
					spawned = true;
				}
			}

			// everything is spawned, proceeed to InWave
			if (!spawned) {
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
	if (gameStatus.lives <= 0) {
		running = false;
	}
}

bool Game::IsRunning()
{
	return running;
}

State Game::GetNextState()
{
	if (gameStatus.lives > 0)
		return ST_WIN;
	return ST_LOOSE;
}

void Game::LoadEnemySettings()
{
	if (level.theme == prevTheme)
		return; // already loaded

	fs::path themePath = GetThemePath(level.theme);
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
	enemySettings.clear();
	enemySettings.resize(enemies.size());
	for (size_t i=0; i < enemies.size(); ++i) {
		js::mObject& def = enemies[i].get_obj();

		enemySettings[i].image     = &gImageManager.getResource((themePath / def["image"].get_str()).string());
		enemySettings[i].width     = def["width"].get_int();
		enemySettings[i].height    = def["height"].get_int();
		enemySettings[i].offset    = def["offset"].get_int();
		enemySettings[i].numFrames = def["frames"].get_int();
		enemySettings[i].frameTime = jsex::get<float>(def["frame-time"]);
		
		enemySettings[i].life  = def["life"].get_int();
		enemySettings[i].speed = jsex::get<float>(def["speed"]);
		enemySettings[i].moneyFactor = def["money-factor"].get_int();
	}
}

void Game::SpawnEnemy(size_t type, size_t spawn)
{
	std::shared_ptr<Enemy> e(new Enemy(enemySettings[type], &map));
	e->SetPosition(map.GetSpawnPosition(spawn));
	e->SetTarget(map.GetDefaultTarget());
	enemies.push_back(e);
}

void Game::AddTower(const TowerSettings* settings, Vector2f pos)
{
	gameStatus.money -= settings->baseCost;

	std::shared_ptr<Tower> tower = Tower::CreateTower(settings, enemies, projectiles, map.IsHighRange(pos));
	tower->SetPosition(pos);
	towers.emplace_back(std::move(tower));
	boost::sort(towers, CompByY);
	map.PlaceTower(pos);
}
