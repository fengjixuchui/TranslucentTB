// Standard API
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

// Windows API
#include "arch.h"
#include <PathCch.h>
#include <sal.h>
#include <ShlObj.h>
#include <winrt/base.h>

// WIL
#include <wil/filesystem.h>

// RapidJSON
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

// Local stuff
#include "config/config.hpp"
#include "constants.hpp"
#include "darkthememanager.hpp"
#include "resources/ids.h"
#include "undoc/swca.hpp"
#include "taskbar/taskbarattributeworker.hpp"
#include "taskdialogs/aboutdialog.hpp"
#include "taskdialogs/welcomedialog.hpp"
#include "tray/traycontextmenu.hpp"
#include "log/ttberror.hpp"
#include "log/ttblog.hpp"
#include "win32.hpp"
#include "windows/messagewindow.hpp"
#include "window.hpp"
#include "windows/windowclass.hpp"
#include "uwp/autostart.hpp"
#include "uwp/uwp.hpp"

#pragma region Data

enum class EXITREASON {
	NewInstance,	 // New instance told us to exit
	UserAction,		 // Triggered by the user
	UserActionNoSave // Triggered by the user, but doesn't saves config
};

static struct {
	EXITREASON exit_reason = EXITREASON::UserAction;
	std::filesystem::path config_folder;
	std::filesystem::path config_file;
} run;

static const std::unordered_map<ACCENT_STATE, uint32_t> DESKTOP_BUTTOM_MAP = {
	{ ACCENT_NORMAL,                     ID_DESKTOP_NORMAL  },
	{ ACCENT_ENABLE_TRANSPARENTGRADIENT, ID_DESKTOP_CLEAR   },
	{ ACCENT_ENABLE_GRADIENT,            ID_DESKTOP_OPAQUE  },
	{ ACCENT_ENABLE_BLURBEHIND,          ID_DESKTOP_BLUR    },
	{ ACCENT_ENABLE_ACRYLICBLURBEHIND,   ID_DESKTOP_ACRYLIC }
};

static const std::unordered_map<ACCENT_STATE, uint32_t> VISIBLE_BUTTOM_MAP = {
	{ ACCENT_NORMAL,                     ID_VISIBLE_NORMAL  },
	{ ACCENT_ENABLE_TRANSPARENTGRADIENT, ID_VISIBLE_CLEAR   },
	{ ACCENT_ENABLE_GRADIENT,            ID_VISIBLE_OPAQUE  },
	{ ACCENT_ENABLE_BLURBEHIND,          ID_VISIBLE_BLUR    },
	{ ACCENT_ENABLE_ACRYLICBLURBEHIND,   ID_VISIBLE_ACRYLIC }
};

static const std::unordered_map<ACCENT_STATE, uint32_t> MAXIMISED_BUTTON_MAP = {
	{ ACCENT_NORMAL,                     ID_MAXIMISED_NORMAL  },
	{ ACCENT_ENABLE_TRANSPARENTGRADIENT, ID_MAXIMISED_CLEAR   },
	{ ACCENT_ENABLE_GRADIENT,            ID_MAXIMISED_OPAQUE  },
	{ ACCENT_ENABLE_BLURBEHIND,          ID_MAXIMISED_BLUR    },
	{ ACCENT_ENABLE_ACRYLICBLURBEHIND,   ID_MAXIMISED_ACRYLIC }
};

static const std::unordered_map<ACCENT_STATE, uint32_t> START_BUTTON_MAP = {
	{ ACCENT_NORMAL,                     ID_START_NORMAL  },
	{ ACCENT_ENABLE_TRANSPARENTGRADIENT, ID_START_CLEAR   },
	{ ACCENT_ENABLE_GRADIENT,            ID_START_OPAQUE  },
	{ ACCENT_ENABLE_BLURBEHIND,          ID_START_BLUR    },
	{ ACCENT_ENABLE_ACRYLICBLURBEHIND,   ID_START_ACRYLIC }
};

static const std::unordered_map<ACCENT_STATE, uint32_t> CORTANA_BUTTON_MAP = {
	{ ACCENT_NORMAL,                     ID_CORTANA_NORMAL  },
	{ ACCENT_ENABLE_TRANSPARENTGRADIENT, ID_CORTANA_CLEAR   },
	{ ACCENT_ENABLE_GRADIENT,            ID_CORTANA_OPAQUE  },
	{ ACCENT_ENABLE_BLURBEHIND,          ID_CORTANA_BLUR    },
	{ ACCENT_ENABLE_ACRYLICBLURBEHIND,   ID_CORTANA_ACRYLIC }
};

static const std::unordered_map<ACCENT_STATE, uint32_t> TIMELINE_BUTTON_MAP = {
	{ ACCENT_NORMAL,                     ID_TIMELINE_NORMAL  },
	{ ACCENT_ENABLE_TRANSPARENTGRADIENT, ID_TIMELINE_CLEAR   },
	{ ACCENT_ENABLE_GRADIENT,            ID_TIMELINE_OPAQUE  },
	{ ACCENT_ENABLE_BLURBEHIND,          ID_TIMELINE_BLUR    },
	{ ACCENT_ENABLE_ACRYLICBLURBEHIND,   ID_TIMELINE_ACRYLIC }
};

static const std::unordered_map<PeekBehavior, uint32_t> PEEK_BUTTON_MAP = {
	{ PeekBehavior::AlwaysShow,                   ID_PEEK_SHOW                       },
	{ PeekBehavior::WindowMaximisedOnMainMonitor, ID_PEEK_DYNAMIC_MAIN_MONITOR       },
	{ PeekBehavior::WindowMaximisedOnAnyMonitor,  ID_PEEK_DYNAMIC_ANY_MONITOR        },
	{ PeekBehavior::DesktopIsForegroundWindow,    ID_PEEK_DYNAMIC_FOREGROUND_DESKTOP },
	{ PeekBehavior::AlwaysHide,                   ID_PEEK_HIDE                       }
};

static const std::unordered_map<spdlog::level::level_enum, uint32_t> LOG_BUTTON_MAP = {
	{ spdlog::level::debug, ID_LOG_DEBUG },
	{ spdlog::level::info,  ID_LOG_INFO  },
	{ spdlog::level::warn,  ID_LOG_WARN  },
	{ spdlog::level::err,   ID_LOG_ERR   },
	{ spdlog::level::off,   ID_LOG_OFF   }
};

#pragma endregion

#pragma region Configuration

void GetPaths()
{
	if (UWP::HasPackageIdentity())
	{
		try
		{
			run.config_folder = static_cast<std::wstring_view>(UWP::GetApplicationFolderPath(UWP::FolderType::Roaming));
		}
		HresultErrorCatch(spdlog::level::critical, L"Getting application folder paths failed!");
	}
	else
	{
		run.config_folder = win32::GetExeLocation().parent_path();
	}

	run.config_file = run.config_folder / CONFIG_FILE;
}

Config LoadConfig(const std::filesystem::path &file)
{
	using namespace rapidjson;

	Config cfg;

	// This check is so that if the file gets deleted for whatever reason while the app is running, default configuration gets restored immediatly.
	if (std::filesystem::is_regular_file(file))
	{
		std::wifstream fileStream(file);

		WIStreamWrapper jsonStream(fileStream);
		GenericDocument<UTF16LE<>> doc;
		doc.ParseStream<kParseCommentsFlag>(jsonStream);

		cfg.Deserialize(doc);
	}

	return cfg;
}

void SaveConfig(const Config &cfg, const std::filesystem::path &file, bool override = false)
{
	if (override || !cfg.DisableSaving)
	{
		using namespace rapidjson;

		std::wofstream fileStream(file);
		fileStream << L"// For reference on this format, see TODO" << std::endl;

		WOStreamWrapper jsonStream(fileStream);
		PrettyWriter<WOStreamWrapper, UTF16LE<>, UTF16LE<>> writer(jsonStream);

		writer.StartObject();
		cfg.Serialize(writer);
		writer.EndObject();
	}
}

void SetConfig(Config &current, Config &&newConfig, TrayIcon &icon)
{
	if (current.HideTray != newConfig.HideTray)
	{
		if (newConfig.HideTray)
		{
			icon.Hide();
		}
		else
		{
			icon.Show();
		}
	}

	if (current.LogVerbosity != newConfig.LogVerbosity)
	{
		Log::SetLevel(newConfig.LogVerbosity);
	}

	current = std::forward<Config>(newConfig);
}

bool CheckAndRunWelcome()
{
	if (!std::filesystem::is_regular_file(run.config_file))
	{
		SaveConfig({ }, run.config_file);
		if (!WelcomeDialog(run.config_file).Run())
		{
			std::filesystem::remove(run.config_file);
			return false;
		}
	}

	// Remove old version config once prompt is accepted.
	std::filesystem::remove_all(run.config_folder / APP_NAME);

	return true;
}

#pragma endregion

#pragma region Tray

void BindColor(TrayContextMenu &tray, unsigned int id, COLORREF &color)
{
	// TODO: no-op
}

template<class T>
void BindByMap(TrayContextMenu &tray, const std::unordered_map<T, unsigned int> &map, std::function<T()> getter, const std::function<void(T)> &setter)
{
	for (const auto &[new_value, id] : map)
	{
		tray.RegisterContextMenuCallback(id, [setter, &new_value]
		{
			setter(new_value);
		});
	}

	const auto [min_p, max_p] = std::minmax_element(map.begin(), map.end(), Util::map_value_compare<T, unsigned int>());

	tray.RegisterCustomRefresh([min = min_p->second, max = max_p->second, get = std::move(getter), &map](TrayContextMenu::Updater updater)
	{
		updater.CheckRadio(min, max, map.at(get()));
	});
}

template<class T>
void BindByMap(TrayContextMenu &tray, const std::unordered_map<T, unsigned int> &map, T &value)
{
	BindByMap<T>(
		tray,
		map,
		[&value] { return value; },
		[&value](T new_value) { value = new_value; }
	);
}

void BindBool(TrayContextMenu &tray, unsigned int item, bool &value)
{
	tray.RegisterContextMenuCallback(item, [&value]
	{
		value = !value;
	});

	tray.RegisterCustomRefresh([item, &value](TrayContextMenu::Updater updater)
	{
		updater.CheckItem(item, value);
	});
}

void BindBoolToEnabled(TrayContextMenu &tray, unsigned int item, bool &value)
{
	tray.RegisterCustomRefresh([item, &value](TrayContextMenu::Updater updater)
	{
		updater.EnableItem(item, value);
	});
}

void BindAppearance(TrayContextMenu &tray, TaskbarAppearance &appearance, unsigned int colorId, const std::unordered_map<ACCENT_STATE, uint32_t> &map)
{
	BindColor(tray, colorId, appearance.Color);
	BindByMap(tray, map, appearance.Accent);
}

void BindAppearance(TrayContextMenu &tray, OptionalTaskbarAppearance &appearance, unsigned int enableId, unsigned int colorId, const std::unordered_map<ACCENT_STATE, uint32_t> &map)
{
	BindBool(tray, enableId, appearance.Enabled);
	BindAppearance(tray, appearance, colorId, map);
	for (const auto &[_, id] : map)
	{
		BindBoolToEnabled(tray, id, appearance.Enabled);
	}
}

void EnableAppearanceColor(TrayContextMenu::Updater updater, unsigned int id, const OptionalTaskbarAppearance &appearance)
{
	updater.EnableItem(id, appearance.Enabled && appearance.Accent != ACCENT_NORMAL);
}

winrt::fire_and_forget RefreshMenu(const Config &cfg, TrayContextMenu::Updater updater)
{
	// Fire off the task and do what we can do before blocking
	updater.EnableItem(ID_AUTOSTART, false);
	updater.CheckItem(ID_AUTOSTART, false);

	winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::ApplicationModel::StartupTaskState> task;
	if (UWP::HasPackageIdentity())
	{
		task = Autostart::GetStartupState();
		updater.SetText(ID_AUTOSTART, L"Querying startup state...");
	}

	
	const auto log_state = Log::GetInitializationState();
	updater.EnableItem(ID_OPENLOG, log_state == Log::Done);
	updater.SetText(ID_OPENLOG, log_state == Log::Done
		? L"Open log file"
		: log_state == Log::Failed
			? L"Error when initializing log file"
			: L"Nothing has been logged yet"
	);

	updater.EnableItem(ID_LOG, log_state != Log::Failed);
	updater.CheckItem(ID_LOG, Log::GetLevel() != spdlog::level::off);

	updater.EnableItem(ID_DESKTOP_COLOR, cfg.DesktopAppearance.Accent != ACCENT_NORMAL);
	EnableAppearanceColor(updater, ID_VISIBLE_COLOR, cfg.VisibleWindowAppearance);
	EnableAppearanceColor(updater, ID_MAXIMISED_COLOR, cfg.MaximisedWindowAppearance);
	EnableAppearanceColor(updater, ID_START_COLOR, cfg.StartOpenedAppearance);
	EnableAppearanceColor(updater, ID_CORTANA_COLOR, cfg.CortanaOpenedAppearance);
	EnableAppearanceColor(updater, ID_TIMELINE_COLOR, cfg.TimelineOpenedAppearance);

	// Block until it finishes
	if (UWP::HasPackageIdentity())
	{
		const auto state = co_await task;
		updater.EnableItem(ID_AUTOSTART,
			!(state == Autostart::StartupState::DisabledByUser || state == Autostart::StartupState::DisabledByPolicy || state == Autostart::StartupState::EnabledByPolicy));
		updater.CheckItem(ID_AUTOSTART, state == Autostart::StartupState::Enabled || state == Autostart::StartupState::EnabledByPolicy);

		std::wstring autostart_text;
		switch (state)
		{
		case Autostart::StartupState::DisabledByUser:
			autostart_text = L"Startup has been disabled in Task Manager";
			break;
		case Autostart::StartupState::DisabledByPolicy:
			autostart_text = L"Startup has been disabled in Group Policy";
			break;
		case Autostart::StartupState::EnabledByPolicy:
			autostart_text = L"Startup has been enabled in Group Policy";
			break;
		case Autostart::StartupState::Enabled:
		case Autostart::StartupState::Disabled:
			autostart_text = L"Open at boot";
		}
		updater.SetText(ID_AUTOSTART, std::move(autostart_text));
	}
}

#pragma endregion

#pragma region Startup

bool IsSingleInstance()
{
	static wil::unique_mutex mutex;

	if (!mutex)
	{
		const bool opened = mutex.try_open(MUTEX_GUID);
		if (!opened)
		{
			mutex.create(MUTEX_GUID);
		}

		return !opened;
	}
	else
	{
		return true;
	}
}

void InitializeTray(HINSTANCE hInstance, Config &cfg)
{
	static MessageWindow window(TRAY_WINDOW, APP_NAME, hInstance);
	DarkThemeManager::EnableDarkModeForWindow(window);

	static TrayContextMenu tray(window, MAKEINTRESOURCE(IDI_TRAYWHITEICON), MAKEINTRESOURCE(IDR_TRAY_MENU), hInstance);
	DarkThemeManager::EnableDarkModeForTrayIcon(tray, MAKEINTRESOURCE(IDI_TRAYWHITEICON), MAKEINTRESOURCE(IDI_TRAYBLACKICON));

	if (cfg.HideTray)
	{
		tray.Hide();
	}

	static TaskbarAttributeWorker worker(hInstance, cfg);
	static const auto watcher = wil::make_folder_change_reader(run.config_folder.c_str(), false, wil::FolderChangeEvents::All, [](wil::FolderChangeEvent, std::wstring_view file)
	{
		if (file.empty() || Util::IgnoreCaseStringEquals(file, CONFIG_FILE))
		{
			// This callback runs on another thread, so we use a message to avoid threading issues.
			window.post_message(WM_FILECHANGED);
		}
	});

	const auto save_and_exit = [&cfg](...)
	{
		SaveConfig(cfg, run.config_file);
		PostQuitMessage(0);
		return TRUE;
	};

	window.RegisterCallback(WM_FILECHANGED, [&cfg](...)
	{
		SetConfig(cfg, LoadConfig(run.config_file), tray);
		return TRUE;
	});

	window.RegisterCallback(WM_CLOSE, save_and_exit);

	window.RegisterCallback(WM_QUERYENDSESSION, [](WPARAM, LPARAM lParam)
	{
		if (lParam & ENDSESSION_CLOSEAPP)
		{
			// The app is being queried if it can close for an update.
			RegisterApplicationRestart(nullptr, 0);
		}
		return TRUE;
	});

	window.RegisterCallback(WM_ENDSESSION, [&cfg](WPARAM wParam, ...)
	{
		if (wParam)
		{
			// The app can be closed anytime after processing this message. Save the settings.
			SaveConfig(cfg, run.config_file);
		}

		return 0;
	});


	BindBool(tray, ID_DESKTOP_ON_PEEK, cfg.UseRegularAppearanceWhenPeeking);
	BindAppearance(tray, cfg.DesktopAppearance, ID_DESKTOP_COLOR, DESKTOP_BUTTOM_MAP);
	BindAppearance(tray, cfg.VisibleWindowAppearance, ID_VISIBLE, ID_VISIBLE_COLOR, VISIBLE_BUTTOM_MAP);
	BindAppearance(tray, cfg.MaximisedWindowAppearance, ID_MAXIMISED, ID_MAXIMISED_COLOR, MAXIMISED_BUTTON_MAP);
	BindAppearance(tray, cfg.StartOpenedAppearance, ID_START, ID_START_COLOR, START_BUTTON_MAP);
	BindAppearance(tray, cfg.CortanaOpenedAppearance, ID_CORTANA, ID_CORTANA_COLOR, CORTANA_BUTTON_MAP);
	BindAppearance(tray, cfg.TimelineOpenedAppearance, ID_TIMELINE, ID_TIMELINE_COLOR, TIMELINE_BUTTON_MAP);


	BindByMap(tray, PEEK_BUTTON_MAP, cfg.Peek);


	tray.RegisterContextMenuCallback(ID_OPENLOG, Log::Open);
	BindByMap<spdlog::level::level_enum>(tray, LOG_BUTTON_MAP, Log::GetLevel, [&cfg](spdlog::level::level_enum new_value)
	{
		Log::SetLevel(new_value);
		cfg.LogVerbosity = new_value;
	});

	tray.RegisterContextMenuCallback(ID_EDITSETTINGS, [&cfg]
	{
		SaveConfig(cfg, run.config_file);
		win32::EditFile(run.config_file);
	});
	tray.RegisterContextMenuCallback(ID_RETURNTODEFAULTSETTINGS, [&cfg]
	{
		// Automatically reloaded by filesystem watcher.
		SaveConfig({ }, run.config_file);
	});
	BindBool(tray, ID_DISABLESAVING, cfg.DisableSaving);
	tray.RegisterContextMenuCallback(ID_HIDETRAY, [&cfg]
	{
		std::wostringstream str;
		str << L"To see the tray icon again, ";
		if (UWP::HasPackageIdentity())
		{
			str << L"reset " APP_NAME " in the Settings app or ";
		}
		str << L"edit the configuration file at "
			<< run.config_file.native() << L".\n\nAre you sure you want to proceed?";
		const int result = MessageBox(Window::NullWindow, str.str().c_str(), APP_NAME, MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND);
		if (result == IDYES)
		{
			cfg.HideTray = true;
			tray.Hide();
			SaveConfig(cfg, run.config_file);
		}
	});
	tray.RegisterContextMenuCallback(ID_DUMPWORKER, std::bind(&TaskbarAttributeWorker::DumpState, &worker));
	tray.RegisterContextMenuCallback(ID_RESETWORKER, std::bind(&TaskbarAttributeWorker::ResetState, &worker));
	tray.RegisterContextMenuCallback(ID_ABOUT, []
	{
		std::thread([]
		{
			AboutDialog().Run();
		}).detach();
	});
	tray.RegisterContextMenuCallback(ID_EXITWITHOUTSAVING, std::bind(&PostQuitMessage, 0));

	if (UWP::HasPackageIdentity())
	{
		tray.RegisterContextMenuCallback(ID_AUTOSTART, []() -> winrt::fire_and_forget
		{
			co_await Autostart::SetStartupState(
				co_await Autostart::GetStartupState() == Autostart::StartupState::Enabled
					? Autostart::StartupState::Disabled
					: Autostart::StartupState::Enabled
			);
		});
	}
	else
	{
		tray.Update().RemoveItem(ID_AUTOSTART);
	}

	tray.RegisterContextMenuCallback(ID_TIPS, std::bind(&win32::OpenLink, L"https://" APP_NAME ".github.io/tips"));
	tray.RegisterContextMenuCallback(ID_EXIT, save_and_exit);


	tray.RegisterCustomRefresh(std::bind(&RefreshMenu, std::ref(cfg), std::placeholders::_1));
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ wchar_t *, _In_ int)
{
	try
	{
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	}
	HresultErrorCatch(spdlog::level::critical, L"Initialization of Windows Runtime failed.");

	Log::Initialize();
	win32::HardenProcess();

	// If there already is another instance running, tell it to exit
	if (!IsSingleInstance())
	{
		Window::Find(TRAY_WINDOW, APP_NAME).send_message(WM_CLOSE);
	}

	DarkThemeManager::AllowDarkModeForApp();

	// TODO: std::filesystem::filesystem_exception handling

	// Get configuration file paths
	GetPaths();

	// If the configuration files don't exist, restore the files and show welcome to the users
	if (!CheckAndRunWelcome())
	{
		return EXIT_FAILURE;
	}

	// Parse our configuration
	static Config cfg = LoadConfig(run.config_file);
	Log::SetLevel(cfg.LogVerbosity);
	//TODO if (!Config::ParseCommandLine())
	//{
	//	return EXIT_SUCCESS;
	//}

	// Initialize GUI
	InitializeTray(hInstance, cfg);

	// Run the main program loop. When this method exits, TranslucentTB itself is about to exit.
	const auto exitCode = MessageWindow::RunMessageLoop();

	// Not uninitializing WinRT apartment here because it will cause issues
	// with destruction of WinRT objects that have a static lifetime.
	// Apartment gets cleaned up by system anyways when the process dies.

	return static_cast<int>(exitCode);
}

#pragma endregion