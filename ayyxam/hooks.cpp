#include "hooks.hpp"
#include "global.hpp"
#include "guard.hpp"

#include <string>

ayyxam::hooks::nt_query_system_information_t ayyxam::hooks::original_nt_query_system_information = nullptr;
ayyxam::hooks::get_adapters_addresses_t ayyxam::hooks::original_get_adapters_addresses = nullptr;
ayyxam::hooks::bit_blt_t ayyxam::hooks::original_bit_blt = nullptr;
ayyxam::hooks::get_property_value_t ayyxam::hooks::original_get_property_value = nullptr;

NTSTATUS WINAPI ayyxam::hooks::nt_query_system_information(SYSTEM_INFORMATION_CLASS system_information_class, PVOID system_information, ULONG system_information_length, PULONG return_length)
{
	// DONT HANDLE OTHER CLASSES
	if (system_information_class != SystemProcessInformation)
		return ayyxam::hooks::original_nt_query_system_information(
				system_information_class, system_information,
				system_information_length, return_length);

	ayyxam::global::console.log("NtQuerySystemInformation called");
	
	// HIDE PROCESSES
	const auto value = ayyxam::hooks::original_nt_query_system_information(
			system_information_class, system_information, 
			system_information_length, return_length);

	// DONT HANDLE UNSUCCESSFUL CALLS
	if (!NT_SUCCESS(value))
		return value;

	// DEFINE STRUCTURE FOR LIST
	struct SYSTEM_PROCESS_INFO
	{
		ULONG                   NextEntryOffset;
		ULONG                   NumberOfThreads;
		LARGE_INTEGER           Reserved[3];
		LARGE_INTEGER           CreateTime;
		LARGE_INTEGER           UserTime;
		LARGE_INTEGER           KernelTime;
		UNICODE_STRING          ImageName;
		ULONG                   BasePriority;
		HANDLE                  ProcessId;
		HANDLE                  InheritedFromProcessId;
	};

	// HELPER FUNCTION: GET NEXT ENTRY IN LINKED LIST
	auto get_next_entry = [](SYSTEM_PROCESS_INFO* entry)
	{
		return reinterpret_cast<SYSTEM_PROCESS_INFO*>(
			reinterpret_cast<std::uintptr_t>(entry) + entry->NextEntryOffset);
	};

	// ITERATE AND HIDE PROCESS
	auto entry = reinterpret_cast<SYSTEM_PROCESS_INFO*>(system_information);
	SYSTEM_PROCESS_INFO* previous_entry = nullptr;
	for (; entry->NextEntryOffset > 0x00; entry = get_next_entry(entry))
	{
		if (entry->ProcessId == reinterpret_cast<HANDLE>(7488) && previous_entry != nullptr)
		{
			// SKIP ENTRY
			previous_entry->NextEntryOffset += entry->NextEntryOffset;
		}

		// SAVE PREVIOUS ENTRY FOR SKIPPING
		previous_entry = entry;
	}

	return value;
}

ULONG WINAPI ayyxam::hooks::get_adapters_addresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES adapter_addresses, PULONG size_pointer)
{
	ayyxam::global::console.log("GetAdaptersAddresses called");

	// CALL ORIGINAL TO HIDE ENTRIES
	const auto result = ayyxam::hooks::original_get_adapters_addresses(family, flags, reserved, adapter_addresses, size_pointer);

	// DO NOT HANDLE ERRORS
	if (!result)
		return result;

	for (
		auto current_entry = adapter_addresses, previous_entry = adapter_addresses;
		current_entry != nullptr; 
		current_entry = current_entry->Next)
	{
		// FILTER BY FRIENDLY NAME
		const auto friendly_name = std::wstring(current_entry->FriendlyName);

		// ITERATE GUARDED ADAPTERS
		for (auto protected_adapter : guard::hidden_adapter)
		{
			if (protected_adapter.compare(friendly_name) != 0)
			{
				// PROTECTED ADAPTER FOUND:
				// IF NOT FIRST ENTRY, SKIP!
				if (previous_entry != current_entry)
				{
					previous_entry->Next = current_entry->Next;
				}
				else
				{
					// RELOCATE ENTIRE STRUCTURE TO OVERRIDE FIRST ENTRY :)

					// CALCULATE SIZE OF FIRST ENTRY
					const auto delta = current_entry->Length;
					const auto remaining_size = *size_pointer - delta;

					ayyxam::global::console.log_formatted<true>("Delta", delta);
					ayyxam::global::console.log_formatted<true>("Remaining size", remaining_size);

					// CACHE ADDRESS TO COPY FROM LATER ON
					const auto copy_next = current_entry->Next;

					// RELOCATE ALL ENTRIES IN LINKED LIST, SKIP FIRST ELEMENT
					for (auto inner_entry = current_entry->Next; inner_entry != nullptr; )
					{
						// CACHE NEXT ADDRESS FOR LATER
						const auto real_next = inner_entry->Next;

						// RELOCATE
						*reinterpret_cast<std::uint8_t**>(&inner_entry->Next) -= delta;

						// CONTINUE ITERATING
						inner_entry = real_next;
					}

					// MOVE OVER ALL OTHER ENTIRES, OVERWRITING OLD
					memcpy(current_entry, copy_next, remaining_size);

				}

				break;
			}
		}
	}

	return result;
}

BOOL __stdcall ayyxam::hooks::bit_blt(HDC hdc, int x, int y, int cx, int cy, HDC hdc_src, int x1, int y1, DWORD rop)
{
	ayyxam::global::console.log("BitBlt called");
	ayyxam::global::console.log_formatted<true>("HDC", hdc);

	// TRYING TO TAKE SCREENSHOT OF ENTIRE SCREEN ?
	if (GetDC(nullptr) == hdc)
	{

		ayyxam::global::console.log("BitBlt taking screenshot of entire screen, hide window!");

		// HIDE WINDOW
		const auto window_handle = FindWindowA("Notepad", nullptr);
		ShowWindow(window_handle, SW_HIDE);

		// SCREENSHOT
		auto result = ayyxam::hooks::original_bit_blt(hdc, x, y, cx, cy, hdc_src, x1, y1, rop);

		// SHOW WINDOW
		ShowWindow(window_handle, SW_SHOW);

		return result;
	}

	return ayyxam::hooks::original_bit_blt(hdc, x, y, cx, cy, hdc_src, x1, y1, rop);
}

std::int32_t __stdcall ayyxam::hooks::get_property_value(void* handle, std::int32_t property_id, void* value)
{
	constexpr auto value_value_id = 0x755D;
	if (property_id == value_value_id)
	{
		auto result = ayyxam::hooks::original_get_property_value(handle, property_id, value);

		if (result == 0x00) // SUCCESS?
		{
			// VALUE URL IS STORED AT 0x08 FROM VALUE STRUCTURE
			class value_structure
			{
			public:
				char pad_0000[8];	//0x0000
				wchar_t* value;		//0x0008
			};
			auto value_object = reinterpret_cast<value_structure*>(value);

			std::wprintf(L"[RawUiaGetPropertyValue] %ws\n", value_object->value);

			// ZERO OUT OLD URL
			auto size_of_url = 0; // CALCULATE SIZE OF URL
			for (; value_object->value[size_of_url]; size_of_url++) { } 
			ZeroMemory(value_object->value, size_of_url * 2);

			ayyxam::global::console.log_formatted("Size", size_of_url * 2);

			// CHANGE TO GOOGLE.COM
			constexpr wchar_t spoofed_url[] = L"https://google.com";
			std::memcpy(value_object->value, spoofed_url, sizeof(spoofed_url));
		}

		return result;
	}

	return ayyxam::hooks::original_get_property_value(handle, property_id, value);
}
