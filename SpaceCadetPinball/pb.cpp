#include "pch.h"
#include "pb.h"


#include "control.h"
#include "fullscrn.h"
#include "high_score.h"
#include "proj.h"
#include "render.h"
#include "loader.h"
#include "midi.h"
#include "nudge.h"
#include "options.h"
#include "timer.h"
#include "winmain.h"
#include "Sound.h"
#include "TBall.h"
#include "TDemo.h"
#include "TEdgeSegment.h"
#include "TLightGroup.h"
#include "TPlunger.h"
#include "TTableLayer.h"
#include "GroupData.h"
#include "partman.h"
#include "score.h"
#include "TFlipper.h"
#include "TPinballTable.h"
#include "TTextBox.h"
#include "translations.h"

TPinballTable* pb::MainTable = nullptr;
DatFile* pb::record_table = nullptr;
int pb::time_ticks = 0;
GameModes pb::game_mode = GameModes::GameOver;
float pb::time_now = 0, pb::time_next = 0, pb::time_ticks_remainder = 0;
float pb::ball_speed_limit, pb::ball_min_smth, pb::ball_inv_smth, pb::ball_collision_dist;
bool pb::FullTiltMode = false, pb::FullTiltDemoMode = false, pb::cheat_mode = false, pb::demo_mode = false;
std::string pb::DatFileName, pb::BasePath;
ImU32 pb::TextBoxColor;
int pb::quickFlag = 0;
TTextBox *pb::InfoTextBox, *pb::MissTextBox;


int pb::init()
{
	float projMat[12];

	if (DatFileName.empty())
		return 1;
	auto dataFilePath = make_path_name(DatFileName);
	record_table = partman::load_records(dataFilePath.c_str(), FullTiltMode);

	auto useBmpFont = 0;
	get_rc_int(Msg::TextBoxUseBitmapFont, &useBmpFont);
	if (useBmpFont)
		score::load_msg_font("pbmsg_ft");

	if (!record_table)
		return 1;

	auto plt = (ColorRgba*)record_table->field_labeled("background", FieldTypes::Palette);
	gdrv::display_palette(plt);

	auto backgroundBmp = record_table->GetBitmap(record_table->record_labeled("background"));
	auto cameraInfoId = record_table->record_labeled("camera_info") + fullscrn::GetResolution();
	auto cameraInfo = (float*)record_table->field(cameraInfoId, FieldTypes::FloatArray);

	/*Full tilt: table size depends on resolution*/
	// SIDE-DRAW - this needs to be adjusted
	auto resInfo = &fullscrn::resolution_array[fullscrn::GetResolution()];

	if (cameraInfo)
	{
		memcpy(&projMat, cameraInfo, sizeof(float) * 4 * 3);
		cameraInfo += 12;

		auto projCenterX = resInfo->TableWidth * 0.5f;
		auto projCenterY = resInfo->TableHeight * 0.5f;
		auto projD = cameraInfo[0];
		auto zMin = cameraInfo[1];
		auto zScaler = cameraInfo[2];
		proj::init(projMat, projD, projCenterX, projCenterY, zMin, zScaler);
	}

	render::init(nullptr, resInfo->TableWidth, resInfo->TableHeight);
	
	// SIDE-DRAW - this copies the side frame/score box.
	gdrv::copy_bitmap(
		render::background_bitmap,
		backgroundBmp->Width,
		backgroundBmp->Height,
		backgroundBmp->XPosition,
		backgroundBmp->YPosition,
		backgroundBmp,
		0,
		0);

	loader::loadfrom(record_table);

	mode_change(GameModes::InGame);

	time_ticks = 0;
	timer::init(150);
	score::init();

	MainTable = new TPinballTable();

	high_score::read();
	auto ball = MainTable->BallList.at(0);
	ball_speed_limit = ball->Offset * 200.0f;
	ball_min_smth = ball->Offset * 0.5f;
	ball_inv_smth = 1.0f / ball_min_smth;
	ball_collision_dist = (ball->Offset + ball_min_smth) * 2.0f;

	int red = 255, green = 255, blue = 255;
	auto fontColor = get_rc_string(Msg::TextBoxColor);
	if (fontColor)
		sscanf(fontColor, "%d %d %d", &red, &green, &blue);
	TextBoxColor = IM_COL32(red, green, blue, 255);

	return 0;
}

int pb::uninit()
{
	score::unload_msg_font();
	loader::unload();
	delete record_table;
	high_score::write();
	delete MainTable;
	MainTable = nullptr;
	timer::uninit();
	render::uninit();
	return 0;
}

void pb::SelectDatFile(const std::vector<const char*>& dataSearchPaths)
{
	DatFileName.clear();
	FullTiltDemoMode = FullTiltMode = false;

	std::string datFileNames[3]
	{
		"CADET.DAT",
		"PINBALL.DAT",
		"DEMO.DAT",
	};

	// Default game data test order: CADET.DAT, PINBALL.DAT, DEMO.DAT
	if (options::Options.Prefer3DPBGameData)
	{
		std::swap(datFileNames[0], datFileNames[1]);
	}
	for (auto path : dataSearchPaths)
	{
		if (!path)
			continue;

		BasePath = path;
		for (const auto& datFileName : datFileNames)
		{
			auto fileName = datFileName;
			for (int i = 0; i < 2; i++)
			{
				if (i == 1)
					std::transform(fileName.begin(), fileName.end(), fileName.begin(),
					               [](unsigned char c) { return std::tolower(c); });

				auto datFilePath = make_path_name(fileName);
				auto datFile = fopenu(datFilePath.c_str(), "r");
				if (datFile)
				{
					fclose(datFile);
					DatFileName = fileName;
					if (datFileName == "CADET.DAT")
						FullTiltMode = true;
					if (datFileName == "DEMO.DAT")
						FullTiltDemoMode = FullTiltMode = true;
					printf("Loading game from: %s\n", datFilePath.c_str());
					return;
				}
			}
		}
	}
}

void pb::reset_table()
{
	if (MainTable)
		MainTable->Message(MessageCode::Reset, 0.0);
}


void pb::firsttime_setup()
{
	render::update();
}

void pb::mode_change(GameModes mode)
{
	switch (mode)
	{
	case GameModes::InGame:
		if (demo_mode)
		{
			winmain::LaunchBallEnabled = false;
			winmain::HighScoresEnabled = false;
			winmain::DemoActive = true;
			if (MainTable)
			{
				if (MainTable->Demo)
					MainTable->Demo->ActiveFlag = 1;
			}
		}
		else
		{
			winmain::LaunchBallEnabled = true;
			winmain::HighScoresEnabled = true;
			winmain::DemoActive = false;
			if (MainTable)
			{
				if (MainTable->Demo)
					MainTable->Demo->ActiveFlag = 0;
			}
		}
		break;
	case GameModes::GameOver:
		winmain::LaunchBallEnabled = false;
		if (!demo_mode)
		{
			winmain::HighScoresEnabled = true;
			winmain::DemoActive = false;
		}
		if (MainTable && MainTable->LightGroup)
			MainTable->LightGroup->Message(MessageCode::TLightGroupGameOverAnimation, 1.4f);
		break;
	}
	game_mode = mode;
}

void pb::toggle_demo()
{
	if (demo_mode)
	{
		demo_mode = false;
		MainTable->Message(MessageCode::Reset, 0.0);
		mode_change(GameModes::GameOver);
		MissTextBox->Clear();
		InfoTextBox->Display(get_rc_string(Msg::STRING125), -1.0);
	}
	else
	{
		replay_level(true);
	}
}

void pb::replay_level(bool demoMode)
{
	demo_mode = demoMode;
	mode_change(GameModes::InGame);
	if (options::Options.Music)
		midi::music_play();
	MainTable->Message(MessageCode::NewGame, static_cast<float>(options::Options.Players));
}

void pb::ballset(float dx, float dy)
{
	// dx and dy are normalized to window, ideally in [-1, 1]
	static constexpr float sensitivity = 7000;

	for (auto ball : MainTable->BallList)
	{
		if (ball->ActiveFlag)
		{
			ball->Direction.X = dx * sensitivity;
			ball->Direction.Y = dy * sensitivity;
			ball->Speed = maths::normalize_2d(ball->Direction);
		}
	}
}

void pb::frame(float dtMilliSec)
{
	if (dtMilliSec > 100)
		dtMilliSec = 100;
	if (dtMilliSec <= 0)
		return;

	float dtSec = dtMilliSec * 0.001f;
	time_next = time_now + dtSec;
	timed_frame(time_now, dtSec, true);
	time_now = time_next;

	dtMilliSec += time_ticks_remainder;
	auto dtWhole = static_cast<int>(dtMilliSec);
	time_ticks_remainder = dtMilliSec - static_cast<float>(dtWhole);
	time_ticks += dtWhole;

	if (nudge::nudged_left || nudge::nudged_right || nudge::nudged_up)
	{
		nudge::nudge_count = dtSec * 4.0f + nudge::nudge_count;
	}
	else
	{
		auto nudgeDec = nudge::nudge_count - dtSec;
		if (nudgeDec <= 0.0f)
			nudgeDec = 0.0;
		nudge::nudge_count = nudgeDec;
	}
	timer::check();
	render::update();
	score::update(MainTable->CurScoreStruct);
	if (!MainTable->TiltLockFlag)
	{
		if (nudge::nudge_count > 0.5f)
		{
			InfoTextBox->Display(get_rc_string(Msg::STRING126), 2.0);
		}
		if (nudge::nudge_count > 1.0f)
			MainTable->tilt(time_now);
	}
}

void pb::timed_frame(float timeNow, float timeDelta, bool drawBalls)
{
	vector2 vec1{}, vec2{};

	for (auto ball : MainTable->BallList)
	{
		if (ball->ActiveFlag != 0)
		{
			auto collComp = ball->CollisionComp;
			if (collComp)
			{
				ball->TimeDelta = timeDelta;
				collComp->FieldEffect(ball, &vec1);
			}
			else
			{
				if (MainTable->ActiveFlag)
				{
					vec2.X = 0.0;
					vec2.Y = 0.0;
					TTableLayer::edge_manager->FieldEffects(ball, &vec2);
					vec2.X = vec2.X * timeDelta;
					vec2.Y = vec2.Y * timeDelta;
					ball->Direction.X = ball->Speed * ball->Direction.X;
					ball->Direction.Y = ball->Speed * ball->Direction.Y;
					maths::vector_add(ball->Direction, vec2);
					ball->Speed = maths::normalize_2d(ball->Direction);
				}

				auto timeDelta2 = timeDelta;
				auto timeNow2 = timeNow;
				for (auto index = 10; timeDelta2 > 0.000001f && index; --index)
				{
					auto time = collide(timeNow2, timeDelta2, ball);
					timeDelta2 -= time;
					timeNow2 += time;
				}
			}
		}
	}

	for (auto flipper : MainTable->FlipperList)
	{
		flipper->UpdateSprite(timeNow);
	}

	if (drawBalls)
	{
		for (auto ball : MainTable->BallList)
		{
			if (ball->ActiveFlag)
				ball->Repaint();
		}
	}
}

void pb::pause_continue()
{
	winmain::single_step ^= true;
	InfoTextBox->Clear();
	MissTextBox->Clear();
	if (winmain::single_step)
	{
		if (MainTable)
			MainTable->Message(MessageCode::Pause, time_now);
		InfoTextBox->Display(get_rc_string(Msg::STRING123), -1.0);
		midi::music_stop();
		Sound::Deactivate();
	}
	else
	{
		if (MainTable)
			MainTable->Message(MessageCode::Resume, 0.0);
		if (!demo_mode)
		{
			const char* text;
			float textTime;
			if (game_mode == GameModes::GameOver)
			{
				textTime = -1.0;
				text = get_rc_string(Msg::STRING125);
			}
			else
			{
				textTime = 5.0;
				text = get_rc_string(Msg::STRING124);
			}
			InfoTextBox->Display(text, textTime);
		}
		if (options::Options.Music && !winmain::single_step)
			midi::music_play();
		Sound::Activate();
	}
}

void pb::loose_focus()
{
	if (MainTable)
		MainTable->Message(MessageCode::LooseFocus, time_now);
}

void pb::InputUp(GameInput input)
{
	if (game_mode != GameModes::InGame || winmain::single_step || demo_mode)
		return;
	
	const auto bindings = options::MapGameInput(input);
	for (const auto binding : bindings)
	{
		switch (binding)
		{
		case GameBindings::LeftFlipper:
			MainTable->Message(MessageCode::LeftFlipperInputReleased, time_now);
			break;
		case GameBindings::RightFlipper:
			MainTable->Message(MessageCode::RightFlipperInputReleased, time_now);
			break;
		case GameBindings::Plunger:
			MainTable->Message(MessageCode::PlungerInputReleased, time_now);
			break;
		case GameBindings::LeftTableBump:
			nudge::un_nudge_right(0, nullptr);
			break;
		case GameBindings::RightTableBump:
			nudge::un_nudge_left(0, nullptr);
			break;
		case GameBindings::BottomTableBump:
			nudge::un_nudge_up(0, nullptr);
			break;
		default: break;
		}
	}
}

void pb::InputDown(GameInput input)
{
	if (options::WaitingForInput()) 
	{
		options::InputDown(input);
		return;
	}

	const auto bindings = options::MapGameInput(input);
	for (const auto binding : bindings)
	{
		winmain::HandleGameBinding(binding);
	}

	if (game_mode != GameModes::InGame || winmain::single_step || demo_mode)
		return;

	if (input.Type == InputTypes::Keyboard)
		control::pbctrl_bdoor_controller(static_cast<char>(input.Value));

	for (const auto binding : bindings)
	{
		switch (binding)
			{
			case GameBindings::LeftFlipper:
				MainTable->Message(MessageCode::LeftFlipperInputPressed, time_now);
				break;
			case GameBindings::RightFlipper:
				MainTable->Message(MessageCode::RightFlipperInputPressed, time_now);
				break;
			case GameBindings::Plunger:
				MainTable->Message(MessageCode::PlungerInputPressed, time_now);
				break;
			case GameBindings::LeftTableBump:
				if (!MainTable->TiltLockFlag)
					nudge::nudge_right();
				break;
			case GameBindings::RightTableBump:
				if (!MainTable->TiltLockFlag)
					nudge::nudge_left();
				break;
			case GameBindings::BottomTableBump:
				if (!MainTable->TiltLockFlag)
					nudge::nudge_up();
				break;
			default: break;
			}
		}

	if (cheat_mode && input.Type == InputTypes::Keyboard)
	{
		switch (input.Value)
		{
		case 'b':
			if (MainTable->AddBall(6.0f, 7.0f))
				MainTable->MultiballCount++;
			break;
		case 'h':
		{
			high_score_struct entry{ {0}, 1000000000 };
			strncpy(entry.Name, get_rc_string(Msg::STRING127), sizeof entry.Name - 1);
			high_score::show_and_set_high_score_dialog({ entry, 1 });
			break;
		}
		case 'r':
			control::cheat_bump_rank();
			break;
		case 's':
			MainTable->AddScore(static_cast<int>(RandFloat() * 1000000.0f));
			break;
		case SDLK_F12:
			MainTable->port_draw();
			break;
		case 'i':
			MainTable->LightGroup->Message(MessageCode::TLightFtTmpOverrideOn, 1.0f);
			break;
		case 'j':
			MainTable->LightGroup->Message(MessageCode::TLightFtTmpOverrideOff, 1.0f);
			break;
		}
	}
}

void pb::launch_ball()
{
	MainTable->Plunger->Message(MessageCode::PlungerLaunchBall, 0.0f);
}

void pb::end_game()
{
	int scores[4]{};
	int scoreIndex[4]{};

	mode_change(GameModes::GameOver);
	int playerCount = MainTable->PlayerCount;

	score_struct_super* scorePtr = MainTable->PlayerScores;
	for (auto index = 0; index < playerCount; ++index)
	{
		scores[index] = scorePtr->ScoreStruct->Score;
		scoreIndex[index] = index;
		++scorePtr;
	}

	for (auto i = 0; i < playerCount; ++i)
	{
		for (auto j = i + 1; j < playerCount; ++j)
		{
			if (scores[j] > scores[i])
			{
				int score = scores[j];
				scores[j] = scores[i];
				scores[i] = score;

				int index = scoreIndex[j];
				scoreIndex[j] = scoreIndex[i];
				scoreIndex[i] = index;
			}
		}
	}

	if (!demo_mode && !MainTable->CheatsUsed)
	{
		for (auto i = 0; i < playerCount; ++i)
		{
			int position = high_score::get_score_position(scores[i]);
			if (position >= 0)
			{
				high_score_struct entry{ {0}, scores[i] };
				const char* playerName;

				switch(scoreIndex[i])
				{
					default:
					case 0: playerName = get_rc_string(Msg::STRING127); break;
					case 1: playerName = get_rc_string(Msg::STRING128); break;
					case 2: playerName = get_rc_string(Msg::STRING129); break;
					case 3: playerName = get_rc_string(Msg::STRING130); break;
				}

				strncpy(entry.Name, playerName, sizeof entry.Name - 1);
				high_score::show_and_set_high_score_dialog({ entry, -1 });
			}
		}
	}
}

void pb::high_scores()
{
	high_score::show_high_score_dialog();
}

void pb::tilt_no_more()
{
	if (MainTable->TiltLockFlag)
		InfoTextBox->Clear();
	MainTable->TiltLockFlag = 0;
	nudge::nudge_count = -2.0;
}

bool pb::chk_highscore()
{
	if (demo_mode)
		return false;
	for (auto i = 0; i < MainTable->PlayerCount; ++i)
	{
		if (high_score::get_score_position(MainTable->PlayerScores[i].ScoreStruct->Score) >= 0)
			return true;
	}
	return false;
}

float pb::collide(float timeNow, float timeDelta, TBall* ball)
{
	ray_type ray{};
	vector2 positionMod{};

	if (ball->ActiveFlag && !ball->CollisionComp)
	{
		if (ball_speed_limit < ball->Speed)
			ball->Speed = ball_speed_limit;

		auto maxDistance = timeDelta * ball->Speed;
		ball->TimeDelta = timeDelta;
		ball->RayMaxDistance = maxDistance;
		ball->TimeNow = timeNow;

		ray.Origin = ball->Position;
		ray.Direction = ball->Direction;
		ray.MaxDistance = maxDistance;
		ray.CollisionMask = ball->CollisionMask;
		ray.TimeNow = timeNow;
		ray.TimeDelta = timeDelta;
		ray.MinDistance = 0.0020000001f;

		TEdgeSegment* edge = nullptr;
		auto distance = TTableLayer::edge_manager->FindCollisionDistance(&ray, ball, &edge);
		ball->EdgeCollisionCount = 0;
		if (distance >= 1000000000.0f)
		{
			maxDistance = timeDelta * ball->Speed;
			ball->RayMaxDistance = maxDistance;
			positionMod.X = maxDistance * ball->Direction.X;
			positionMod.Y = maxDistance * ball->Direction.Y;
			maths::vector_add(ball->Position, positionMod);
		}
		else
		{
			edge->EdgeCollision(ball, distance);
			if (ball->Speed > 0.000000001f)
				return fabs(distance / ball->Speed);
		}
	}
	return timeDelta;
}

void pb::PushCheat(const std::string& cheat)
{
	for (auto ch : cheat)
		control::pbctrl_bdoor_controller(ch);
}

LPCSTR pb::get_rc_string(Msg uID)
{
	return translations::GetTranslation(uID);
}

int pb::get_rc_int(Msg uID, int* dst)
{
	*dst = atoi(get_rc_string(uID));
	return 1;
}

std::string pb::make_path_name(const std::string& fileName)
{
	return BasePath + fileName;
}

void pb::ShowMessageBox(Uint32 flags, LPCSTR title, LPCSTR message)
{
	fprintf(flags == SDL_MESSAGEBOX_ERROR ? stderr : stdout, "BL error: %s\n%s\n", title, message);
	SDL_ShowSimpleMessageBox(flags, title, message, winmain::MainWindow);
}
