#include "PCH.h"

#include <Windows.h>

namespace Papyrus
{
    std::vector<std::int32_t> GetAllResourceActorValueIDs(std::monostate)
    {
        std::vector<std::int32_t> result;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return result;

        auto formTypeIdx = std::to_underlying(RE::FormType::kIRES);
        auto& resourceForms = dataHandler->formArrays[formTypeIdx];
        const RE::BSAutoReadLock locker(resourceForms.lock);

        spdlog::info("GetAllResourceActorValueIDs: formType index={}, formArray size={}",
            formTypeIdx, resourceForms.formArray.size());

        for (auto& formPtr : resourceForms.formArray) {
            if (!formPtr) {
                continue;
            }

            auto* resource = formPtr->As<RE::BGSResource>();
            if (!resource) {
                spdlog::debug("  Form 0x{:08X} failed As<BGSResource>()", formPtr->GetFormID());
                continue;
            }

            if (!resource->actorValue) {
                spdlog::debug("  Resource '{}' has null actorValue",
                    resource->GetFormEditorID() ? resource->GetFormEditorID() : "?");
                continue;
            }

            result.push_back(static_cast<std::int32_t>(resource->actorValue->GetFormID()));
            spdlog::debug("  Resource '{}' -> AV 0x{:08X}",
                resource->GetFormEditorID() ? resource->GetFormEditorID() : "?",
                resource->actorValue->GetFormID());
        }

        spdlog::info("GetAllResourceActorValueIDs: returning {} resource AVs", result.size());
        return result;
    }

    void Register()
    {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            spdlog::error("Failed to get VM singleton");
            return;
        }

        auto* ivm = static_cast<RE::BSScript::IVirtualMachine*>(vm);
        ivm->BindNativeMethod(
            "CompletePlanetSurveyQuest"sv,
            "GetAllResourceActorValueIDs"sv,
            &GetAllResourceActorValueIDs,
            std::optional<bool>{ true },
            false);

        spdlog::info("Registered native Papyrus function: GetAllResourceActorValueIDs");
    }
}

namespace
{
    bool g_keyWasDown = false;

    void CompletePlanetSurvey()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto* parentCell = player->parentCell;
        if (!parentCell || parentCell->IsInterior()) {
            RE::DebugNotification("Survey: Must be on a planet surface");
            return;
        }

        auto* worldSpace = parentCell->cellWorldspace;
        if (!worldSpace) {
            RE::DebugNotification("Survey: No worldspace found");
            return;
        }

        RE::DebugNotification("Survey: Completing planet survey...");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        // 1. Traits: set discovery chance to 100%
        auto* chanceGlobal = RE::TESForm::LookupByID<RE::TESGlobal>(0x002508F2);
        if (chanceGlobal) {
            chanceGlobal->value = 100.0f;
        }

        // 2. Flora/fauna scan counts to 1
        auto* gmst = RE::GameSettingCollection::GetSingleton();
        if (gmst) {
            gmst->SetSetting("iHandScannerAnimalCountBase", static_cast<std::int32_t>(1));
            gmst->SetSetting("iHandScannerPlantsCountBase", static_cast<std::int32_t>(1));
        }

        // 3. Mark all planet locations as explored
        auto* wsLocation = worldSpace->location.get();
        if (dataHandler && wsLocation) {
            if (!wsLocation->explored) {
                wsLocation->explored = true;
                wsLocation->everExplored = true;
            }

            auto& locationForms = dataHandler->formArrays[std::to_underlying(RE::FormType::kLCTN)];
            const RE::BSAutoReadLock locker(locationForms.lock);

            for (auto& formPtr : locationForms.formArray) {
                if (!formPtr) continue;
                auto* location = formPtr->As<RE::BGSLocation>();
                if (!location || location->explored) continue;

                auto* parent = location->parentLocation.get();
                while (parent) {
                    if (parent == wsLocation) {
                        location->explored = true;
                        location->everExplored = true;
                        break;
                    }
                    parent = parent->parentLocation.get();
                }
            }
        }

        // 4. Scan trait POIs in loaded cells
        auto scanCell = [](RE::TESObjectCELL* cell) {
            if (!cell || !cell->IsAttached()) return;
            cell->ForEachReference([](const RE::NiPointer<RE::TESObjectREFR>& ref) -> RE::BSContainer::ForEachResult {
                if (!ref) return RE::BSContainer::ForEachResult::kContinue;
                auto baseObject = ref->GetBaseObject();
                if (!baseObject) return RE::BSContainer::ForEachResult::kContinue;

                if (baseObject->GetFormType() == RE::FormType::kBMMO) {
                    auto* refLocation = ref->GetCurrentLocation();
                    if (refLocation && !refLocation->explored) {
                        refLocation->explored = true;
                        refLocation->everExplored = true;
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        };

        scanCell(parentCell);
        if (auto pc = worldSpace->unk0D0.get()) scanCell(pc);
        if (auto pc = worldSpace->unk1D0.get()) scanCell(pc);

        RE::DebugNotification("Survey: Done. Run: cgf \"CompletePlanetSurveyQuest.CompleteSurvey\"");
    }

    struct PlayerUpdateHook
    {
        static void thunk(RE::PlayerCharacter* a_player, float a_delta)
        {
            func(a_player, a_delta);
            bool keyIsDown = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
            if (keyIsDown && !g_keyWasDown) {
                CompletePlanetSurvey();
            }
            g_keyWasDown = keyIsDown;
        }
        static inline constexpr std::size_t idx{ 0xC8 };
        static inline REL::Relocation<decltype(thunk)> func;
    };

    void MessageCallback(SFSE::MessagingInterface::Message* a_msg) noexcept
    {
        if (a_msg->type == SFSE::MessagingInterface::kPostDataLoad) {
            // Hook player update for hotkey
            REL::Relocation vtbl{ RE::PlayerCharacter::VTABLE[0] };
            PlayerUpdateHook::func = vtbl.write_vfunc(PlayerUpdateHook::idx, PlayerUpdateHook::thunk);

            Papyrus::Register();
            spdlog::info("Complete Planet Survey initialized (F8 + native Papyrus bridge)");
        }
    }
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
    SFSE::Init(a_sfse);
    spdlog::info("{} v{} loading", Plugin::Name, Plugin::Version.string());

    const auto* messaging = SFSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageCallback)) {
        spdlog::critical("Failed to register messaging listener");
        return false;
    }

    spdlog::info("{} v{} loaded", Plugin::Name, Plugin::Version.string());
    return true;
}
