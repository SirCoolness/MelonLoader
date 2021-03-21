#include <string>
#include "Mono.h"
#include "Game.h"
#include "Hook.h"
#include "../Utils/Assertion.h"
#include "../Utils/Console/CommandLine.h"
#include "../Utils/Console/Debug.h"
#include "../Base/Core.h"
#include "InternalCalls.h"
#include "BaseAssembly.h"
#include "Il2Cpp.h"
#include "../Utils/Console/Logger.h"
#include "../Utils/Helpers/ImportLibHelper.h"

#ifdef __ANDROID__
#include <dlfcn.h>
#include <stdio.h>
#include <sys/stat.h>
#endif

#ifdef _WIN32
const char* Mono::LibNames[] = { "mono", "mono-2.0-bdwgc", "mono-2.0-sgen", "mono-2.0-boehm" };
#elif defined(__ANDROID__)
const char* Mono::LibNames[] = { "monobdwgc-2.0", "monosgen-2.0" };
#endif

const char* Mono::FolderNames[] = { "Mono", "MonoBleedingEdge", "MonoBleedingEdge.x86", "MonoBleedingEdge.x64" };
const char* Mono::PosixHelperName = "MonoPosixHelper";
char* Mono::BasePath = NULL;
char* Mono::ManagedPath = NULL;
char* Mono::ConfigPath = NULL;
Mono::Domain* Mono::domain = NULL;
bool Mono::IsOldMono = false;

#ifdef _WIN32
HMODULE Mono::Module = NULL;
HMODULE Mono::PosixHelper = NULL;
#elif defined(__ANDROID__)
void* Mono::Module = NULL;
void* Mono::PosixHelper = NULL;

Patcher* Mono::Patches::mono_unity_get_unitytls_interface = NULL;
#endif

#pragma region MonoDeclare
#define MONODEF(fn) Mono::Exports::fn##_t Mono::Exports::fn = NULL;

MONODEF(mono_jit_init)
MONODEF(mono_jit_init_version)
MONODEF(mono_set_assemblies_path)
MONODEF(mono_assembly_setrootdir)
MONODEF(mono_set_config_dir)
MONODEF(mono_runtime_set_main_args)
MONODEF(mono_thread_set_main)
MONODEF(mono_thread_current)
MONODEF(mono_domain_set_config)
MONODEF(mono_add_internal_call)
MONODEF(mono_lookup_internal_call)
MONODEF(mono_runtime_invoke)
MONODEF(mono_method_get_name)
MONODEF(mono_unity_get_unitytls_interface)
MONODEF(mono_domain_assembly_open)
MONODEF(mono_assembly_get_image)
MONODEF(mono_class_from_name)
MONODEF(mono_class_get_method_from_name)
MONODEF(mono_string_to_utf8)
MONODEF(mono_string_new)
MONODEF(mono_object_get_class)
MONODEF(mono_class_get_property_from_name)
MONODEF(mono_property_get_get_method)
MONODEF(mono_free)
MONODEF(g_free)

MONODEF(mono_raise_exception)
MONODEF(mono_get_exception_bad_image_format)
MONODEF(mono_image_open_full)
MONODEF(mono_image_open_from_data_full)
MONODEF(mono_image_close)
MONODEF(mono_image_get_table_rows)
MONODEF(mono_metadata_decode_table_row_col)
MONODEF(mono_array_addr_with_size)
MONODEF(mono_array_length)
MONODEF(mono_metadata_string_heap)
MONODEF(mono_class_get_name)

#undef MONODEF
#pragma endregion MonoDeclare

bool Mono::Initialize()
{
	Debug::Msg("Initializing Mono...");

	if (!SetupPaths())
	{
		Assertion::ThrowInternalFailure("Failed to Setup Mono Paths!");
		return false;
	}
	Debug::Msg(("Mono::BasePath = " + std::string(BasePath)).c_str());
	Debug::Msg(("Mono::ManagedPath = " + std::string(ManagedPath)).c_str());
	Debug::Msg(("Mono::ConfigPath = " + std::string(ConfigPath)).c_str());
	IsOldMono = Core::FileExists((std::string(BasePath) + "\\mono.dll").c_str());
	Debug::Msg(("Mono::IsOldMono = " + std::string((IsOldMono ? "true" : "false"))).c_str());
	
	return true;
}

bool Mono::Load()
{
	for (int i = 0; i < (sizeof(LibNames) / sizeof(LibNames[0])); i++)
	{
#ifdef _WIN32
		Module = LoadLibraryA((std::string(BasePath) + "\\" + LibNames[i] + ".dll").c_str());
#elif defined (__ANDROID__)
		Debug::Msg((std::string("trying: ") + std::string("lib") + std::string(LibNames[i]) + ".so").c_str());
		Mono::Module = dlopen((std::string("lib") + std::string(LibNames[i]) + ".so").c_str(), RTLD_NOW & RTLD_NODELETE & RTLD_GLOBAL);
#endif
		
		if (Module != NULL)
		{
#ifndef __ANDROID__
			if (i == 0)
				IsOldMono = true;
#endif
			break;
		}
	}
	
	if (Module == NULL)
	{
		Assertion::ThrowInternalFailure("Failed to Load Mono Library!");
		return false;
	}

#ifdef _WIN32
	PosixHelper = LoadLibraryA((std::string(BasePath) + "\\" + Mono::PosixHelperName + ".dll").c_str());
#elif defined(__ANDROID__)
	PosixHelper = dlopen((std::string("lib") + std::string(Mono::PosixHelperName) + ".so").c_str(), RTLD_NOW & RTLD_NODELETE & RTLD_GLOBAL);
#endif

	if ((PosixHelper == NULL) && !IsOldMono)
	{
		Assertion::ThrowInternalFailure("Failed to Load Mono Posix Helper!");
		return false;
	}
	
	return Exports::Initialize();
}

bool Mono::SetupPaths()
{
#ifdef _WIN32
	std::string MonoDir = std::string();
	for (int i = 0; i < (sizeof(FolderNames) / sizeof(FolderNames[0])); i++)
	{
		if (Game::IsIl2Cpp)
		{
			std::string str_melon = (std::string(Game::BasePath) + "\\MelonLoader\\Dependencies\\" + FolderNames[i]);
			if (Core::DirectoryExists(str_melon.c_str()))
			{
				MonoDir = str_melon;
				break;
			}
		}
		else
		{
			std::string str_base = (std::string(Game::BasePath) + "\\" + FolderNames[i]);
			if (Core::DirectoryExists(str_base.c_str()))
			{
				MonoDir = str_base;
				break;
			}
			std::string str_data = (std::string(Game::DataPath) + "\\" + FolderNames[i]);
			if (Core::DirectoryExists(str_data.c_str()))
			{
				MonoDir = str_data;
				break;
			}
		}
	}
	if (MonoDir.empty())
	{
		Assertion::ThrowInternalFailure("Failed to Find Mono Directory!");
		return false;
	}

	if (Game::IsIl2Cpp)
	{
		BasePath = new char[MonoDir.size() + 1];
		std::copy(MonoDir.begin(), MonoDir.end(), BasePath);
		BasePath[MonoDir.size()] = '\0';

		std::string ManagedPathStr = (std::string(Game::BasePath) + "\\MelonLoader\\Managed");
		ManagedPath = new char[ManagedPathStr.size() + 1];
		std::copy(ManagedPathStr.begin(), ManagedPathStr.end(), ManagedPath);
		ManagedPath[ManagedPathStr.size()] = '\0';

		std::string ConfigPathStr = (std::string(Game::DataPath) + "\\il2cpp_data\\etc");
		ConfigPath = new char[ConfigPathStr.size() + 1];
		std::copy(ConfigPathStr.begin(), ConfigPathStr.end(), ConfigPath);
		ConfigPath[ConfigPathStr.size()] = '\0';

		return true;
	}

	std::string BasePathStr = (MonoDir + "\\EmbedRuntime");
	if (!Core::DirectoryExists(BasePathStr.c_str()))
		BasePathStr = MonoDir;
	BasePath = new char[BasePathStr.size() + 1];
	std::copy(BasePathStr.begin(), BasePathStr.end(), BasePath);
	BasePath[BasePathStr.size()] = '\0';

	std::string ManagedPathStr = (std::string(Game::DataPath) + "\\Managed");
	ManagedPath = new char[ManagedPathStr.size() + 1];
	std::copy(ManagedPathStr.begin(), ManagedPathStr.end(), ManagedPath);
	ManagedPath[ManagedPathStr.size()] = '\0';

	std::string ConfigPathStr = (MonoDir + "\\etc");
	ConfigPath = new char[ConfigPathStr.size() + 1];
	std::copy(ConfigPathStr.begin(), ConfigPathStr.end(), ConfigPath);
	ConfigPath[ConfigPathStr.size()] = '\0';
#elif defined(__ANDROID__)
	if (Game::IsIl2Cpp)
	{
		std::string BasePathStr = (std::string(Game::BasePath) + "/melonloader/etc").c_str();
		BasePath = new char[BasePathStr.size() + 1];
		std::copy(BasePathStr.begin(), BasePathStr.end(), BasePath);
		BasePath[BasePathStr.size()] = '\0';

		std::string ManagedPathStr = (std::string(Mono::BasePath) + "/managed").c_str();
		ManagedPath = new char[ManagedPathStr.size() + 1];
		std::copy(ManagedPathStr.begin(), ManagedPathStr.end(), ManagedPath);
		ManagedPath[ManagedPathStr.size()] = '\0';
		
		std::string ConfigPathStr = (std::string(Mono::BasePath) + "/config").c_str();
		ConfigPath = new char[ConfigPathStr.size() + 1];
		std::copy(ConfigPathStr.begin(), ConfigPathStr.end(), ConfigPath);
		ConfigPath[ConfigPathStr.size()] = '\0';


		char* Paths[] = {
			BasePath,
			ManagedPath,
			ConfigPath
		};

		for (auto& path : Paths)
		{
			if (!Core::DirectoryExists(path))
			{
				Assertion::ThrowInternalFailure((std::string("Failed to load path (") + path + ") because it doesn't exist.").c_str());
				return false;
			}
		}

		return true;
	}

	Assertion::ThrowInternalFailure("(mono) NOT IMPLEMENTED");
	return false;
#endif
	
	return true;
}

void Mono::CreateDomain(const char* name)
{
	if (domain != NULL)
		return;

	Debug::Msg("Creating Mono Domain...");

	Exports::mono_set_assemblies_path(ManagedPath);
	Exports::mono_assembly_setrootdir(ManagedPath);
	Exports::mono_set_config_dir(ConfigPath);

#ifdef PORT_DISABLE
	if (!IsOldMono)
		Exports::mono_runtime_set_main_args(CommandLine::argc, CommandLine::argv);
#endif

	Debug::Msg("Jit init");
	
	domain = Exports::mono_jit_init(name);

	Debug::Msg("set thread");
	
	Exports::mono_thread_set_main(Exports::mono_thread_current());
	
	if (!IsOldMono)
		Exports::mono_domain_set_config(domain, Game::BasePath, name);
}

void Mono::AddInternalCall(const char* name, void* method)
{
	Debug::Msg(name);
	Exports::mono_add_internal_call(name, method);
}

void Mono::Free(void* ptr)
{
	if (IsOldMono)
		Exports::g_free(ptr);
	else
		Exports::mono_free(ptr);
}

bool Mono::Exports::Initialize()
{
	Debug::Msg("Initializing Mono Exports...");
	
#pragma region MonoBind
#define MONODEF(fn) fn = (fn##_t) ImportLibHelper::GetExport(Module, #fn);

	MONODEF(mono_jit_init)
	MONODEF(mono_thread_set_main)
	MONODEF(mono_thread_current)
	MONODEF(mono_add_internal_call)
	MONODEF(mono_lookup_internal_call)
	MONODEF(mono_runtime_invoke)
	MONODEF(mono_method_get_name)
	MONODEF(mono_domain_assembly_open)
	MONODEF(mono_assembly_get_image)
	MONODEF(mono_class_from_name)
	MONODEF(mono_class_get_method_from_name)
	MONODEF(mono_string_to_utf8)
	MONODEF(mono_string_new)
	MONODEF(mono_object_get_class)
	MONODEF(mono_class_get_property_from_name)
	MONODEF(mono_property_get_get_method)

	if (!IsOldMono)
	{
		MONODEF(mono_domain_set_config)
		MONODEF(mono_unity_get_unitytls_interface)
		MONODEF(mono_free)
	}
	else
		MONODEF(g_free)

	if (Game::IsIl2Cpp)
	{
		MONODEF(mono_set_assemblies_path)
		MONODEF(mono_assembly_setrootdir)
		MONODEF(mono_set_config_dir)
			
		if (!IsOldMono)
			MONODEF(mono_runtime_set_main_args)
			
		MONODEF(mono_raise_exception)
		MONODEF(mono_get_exception_bad_image_format)
		MONODEF(mono_image_open_full)
		MONODEF(mono_image_open_from_data_full)
		MONODEF(mono_image_close)
		MONODEF(mono_image_get_table_rows)
		MONODEF(mono_metadata_decode_table_row_col)
		MONODEF(mono_array_addr_with_size)
		MONODEF(mono_array_length)
		MONODEF(mono_metadata_string_heap)
		MONODEF(mono_class_get_name)
	}
	else
		MONODEF(mono_jit_init_version)

#undef MONODEF
#pragma endregion MonoBind

	return Assertion::ShouldContinue;
}

void Mono::LogException(Mono::Object* exceptionObject, bool shouldThrow)
{
	if (exceptionObject == NULL)
		return;

	Class* klass = Exports::mono_object_get_class(exceptionObject);
	if (klass == NULL)
		return;

	Property* prop = Exports::mono_class_get_property_from_name(klass, "Message");
	if (prop == NULL)
		return;
	
	Method* method = Exports::mono_property_get_get_method(prop);
	if (method == NULL)
		return;
	
	String* returnstr = (String*)Exports::mono_runtime_invoke(method, exceptionObject, NULL, NULL);
	if (returnstr == NULL)
		return;
	
	const char* returnstrc = Exports::mono_string_to_utf8(returnstr);
	if (returnstrc == NULL)
		return;
	
	Logger::Error(returnstrc);
	Exports::mono_free(returnstr);
}


#ifdef __ANDROID__
bool Mono::ApplyPatches()
{
	Patches::mono_unity_get_unitytls_interface = new Patcher((void**)&Exports::mono_unity_get_unitytls_interface, (void*)Hooks::mono_unity_get_unitytls_interface);

	Patches::mono_unity_get_unitytls_interface->ApplyPatch();

	return true;
}
#endif

#pragma region Hooks

void* Mono::Hooks::mono_unity_get_unitytls_interface() { return Il2Cpp::UnityTLSInterfaceStruct; }

#ifdef _WIN32
Mono::Domain* Mono::Hooks::mono_jit_init_version(const char* name, const char* version)
{
	if (!Debug::Enabled)
		Console::SetHandles();
	
	Debug::Msg("Detaching Hook from mono_jit_init_version...");
	Hook::Detach(&(LPVOID&)Exports::mono_jit_init_version, mono_jit_init_version);
	
	Debug::Msg("Creating Mono Domain...");
	domain = Exports::mono_jit_init_version(name, version);
	Exports::mono_thread_set_main(Exports::mono_thread_current());
	if (!IsOldMono)
		Exports::mono_domain_set_config(domain, Game::BasePath, name);
	InternalCalls::Initialize();
	if (BaseAssembly::Initialize())
	{
		Debug::Msg("Attaching Hook to mono_runtime_invoke...");
		Hook::Attach(&(LPVOID&)Exports::mono_runtime_invoke, mono_runtime_invoke);
	}
	return domain;
}

Mono::Object* Mono::Hooks::mono_runtime_invoke(Method* method, Object* obj, void** params, Object** exec)
{
	const char* method_name = Exports::mono_method_get_name(method);
	if ((strstr(method_name, "Internal_ActiveSceneChanged") != NULL) || (strstr(method_name, "UnityEngine.ISerializationCallbackReceiver.OnAfterDeserialize") != NULL))
	{
		Debug::Msg("Detaching Hook from mono_runtime_invoke...");
		Hook::Detach(&(LPVOID&)Exports::mono_runtime_invoke, mono_runtime_invoke);
		BaseAssembly::Start();
	}
	return Exports::mono_runtime_invoke(method, obj, params, exec);
}
#endif
#pragma endregion Hooks