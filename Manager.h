#pragma once

#include "Settings.h"



#include <iostream>
#include <unordered_set>

class Manager {
    
    // private variables
    unsigned int n_bags = 0;
    unsigned int n_chests = 0;
    std::vector<std::string> buttons = {"Open", "Activate", "Cancel"};
    RE::TESObjectREFR* player_ref = RE::PlayerCharacter::GetSingleton()->As<RE::TESObjectREFR>();
    RE::TESObjectREFR* current_container = nullptr;

    // unowned stuff
    RE::TESObjectCELL* unownedCell = RE::TESForm::LookupByID<RE::TESObjectCELL>(0x000EA28B);
    RE::TESObjectCONT* unownedChest = RE::TESForm::LookupByID<RE::TESObjectCONT>(0x000EA299);
    RE::TESObjectREFR* unownedChestObjRef;
    RE::NiPoint3 unownedChestPos = {1989.0740f, 1793.2015f, 6784.0000f};


    // private functions
    RE::TESObjectREFR* MakeChest(RE::NiPoint3 Pos3 = {0.0f, 0.0f, 0.0f}) {
        auto item = unownedChest->As<RE::TESBoundObject>();

        auto newPropRef = RE::TESDataHandler::GetSingleton()
                              ->CreateReferenceAtLocation(item, Pos3, {0.0f, 0.0f, 0.0f}, unownedCell, nullptr, nullptr,
                                                          nullptr, {}, true, false).get().get();
        logger::info("Created Object! Type: {}, Base ID: {:x}, Ref ID: {:x},",
                     RE::FormTypeToString(item->GetFormType()), item->GetFormID(), newPropRef->GetFormID());

        return newPropRef;
    };

    
    Source* GetContainerSource(const RE::TESObjectREFR* a_container) {
        logger::info("Getting container source");
        auto container_formid = a_container->GetBaseObject()->GetFormID();
        for (auto& src : sources) {
            if (src.formid == container_formid) return &src;
		}
		return nullptr;
    };


    const ItemListData GetSavedItems(RE::TESObjectREFR* a_container) {
        logger::info("Getting saved items");
        const auto src = GetContainerSource(a_container);
        auto it = src->data.find(GetEditorRefID(a_container));
        if (it != src->data.end()) return it->second;
        return ItemListData();
    };

    void UpdateItemListData(RE::TESObjectREFR* container, const RE::TESObjectREFR::InventoryCountMap inventory_map) {
        if (!container) return;
        logger::info("Updating item list data");
        auto src = GetContainerSource(container);
        src->data[GetEditorRefID(container)] = InventoryCountMap2ItemListData(inventory_map);
        logger::info("Item list data updated");
        src->PrintSourceEditorRefIDs();
    };


    void RemoveAllItemsFromChest(bool _updateItemListData = false ) { 
        auto items = unownedChestObjRef->GetInventoryCounts();
        logger::info("Removing {} items from chest", items.size());

        // Save the content of the chest
        if (_updateItemListData) {
            if (!current_container) logger::info("Current container is null!!!! How???");
			else UpdateItemListData(current_container, items);
        }

        for (const auto& item : items) {
            unownedChestObjRef->RemoveItem(item.first, item.second, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
	};


    bool AddItemToChest(const Utilities::Types::EditorID& editorID, const unsigned int n_item) {
        auto item = RE::TESForm::LookupByEditorID<RE::TESBoundObject>(editorID);
        if (!item) return false;
        unownedChestObjRef->AddObjectToContainer(item, nullptr, n_item, nullptr); // a_fromref player_ref mi olsa daa ii?
        return true;
    };

    void AddItems2Chest(const ItemListData itemlist){
        for (const auto& item : itemlist) {
            const Utilities::Types::EditorID& editorID = item.first;
            const unsigned int n_item = item.second;
            if (!AddItemToChest(editorID, n_item)) {
                logger::error("Could not find item with editorid {}", editorID);
                Utilities::MsgBoxesNotifs::InGame::ProblemWithItem(editorID);
            };
        }
    };


    void Activate(RE::TESObjectREFR* a_objref) {
        auto player = RE::PlayerCharacter::GetSingleton();
        auto a_obj = a_objref->GetBaseObject()->As<RE::TESObjectCONT>();
        if (!a_obj) return;
        a_obj->Activate(a_objref, player, 0, a_obj, 1);
    }

    void ActivateChest(const char* container_name) {
        unownedChest->fullName = container_name;
        Activate(unownedChestObjRef);
    };
    
    void MsgBoxCallback(unsigned int result, RE::TESObjectREFR* a_container) {
        logger::info("Result: {}", result);

        if (result != 0 && result != 1 && result != 2) return;

        if (result == 2) return;

        if (result == 1) {
            listen_activate = false; 
            a_container->SetActivationBlocked(0);
            logger::info("ref id: {}", a_container->GetFormID());
            a_container->GetBaseObject()->Activate(a_container, player_ref, 0, a_container->GetBaseObject(), 1);
            logger::info("ref id: {}", a_container->GetFormID());

            a_container->SetActivationBlocked(1);
            listen_activate = true;
            return;
        }

        // Opening container

        // 1. Add saved items to the unowned chest
        logger::info("Adding items to chest");
        ItemListData items = GetSavedItems(a_container);
        AddItems2Chest(items);

        // 3. Listen for menu close
        listen_menuclose = true;

        // 4. Save last container
        current_container = a_container;

        // 4. Activate the unowned chest
        logger::info("Activating chest");
        ActivateChest(a_container->GetName());

    };

    void InitFailed() {
		logger::error("Failed to initialize Manager.");
		Utilities::MsgBoxesNotifs::InGame::InitErr();
		sources.clear();
		return;
	}

    void Init() {
        bool init_failed = false;

        if (!sources.size()) {
			logger::error("No sources found.");
            InitFailed();
            return;
        }


        for (auto& src : sources) {
            if (!Utilities::GetFormByID(src.formid, src.editorid) 
                || !src.GetBoundObject()) {
                init_failed = true;
                logger::error("Failed to initialize Manager due to missing source");
                break;
            }
        }

        std::unordered_set<std::uint32_t> encounteredFormIDs;

        for (const auto& source : sources) {
            // Check if the formid is already encountered
            if (!encounteredFormIDs.insert(source.formid).second) {
                // Duplicate formid found
                logger::error("Duplicate formid found: {}", source.formid);
                init_failed = true;
            }
        }


        // Check if unowned chest is in the cell
        auto runtimeData = unownedCell->GetRuntimeData();
        if (!unownedCell || !unownedChest || !unownedChest->As<RE::TESBoundObject>()) {
            logger::error("Failed to initialize Manager due to missing unowned chest/cell");
			init_failed = true;
		}


        for (const auto& ref : runtimeData.references) {
            logger::info("Girdik");
            if (!ref) logger::info("Null object in cell");
            else if (ref->GetBaseObject()->GetFormID() == unownedChest->GetFormID()) {
                logger::info("Chest ref found in cell");
                auto chest_ref_count = unownedChest->GetRefCount();
                logger::info("Chest ref count {}", chest_ref_count);
                unownedChestObjRef = ref.get();
                RemoveAllItemsFromChest();
                break;
            }
        }

        if (!unownedChestObjRef) {
            init_failed = true;
			/*logger::info("Chest ref not found in cell. Adding...");
			auto newPropRef = MakeChest();
			unownedChestObjRef = newPropRef;*/
		}
        
        if (init_failed) InitFailed();

        logger::info("Manager initialized.");
    }

public:
    Manager(std::vector<Source>& data) : sources(data) { Init(); };

    static Manager* GetSingleton(std::vector<Source>& data) {
        static Manager singleton(data);
        return &singleton;
    }

    std::vector<Source> sources;
    bool listen_menuclose = false;
    bool listen_activate = true;

    bool RefIsContainer(RE::TESObjectREFR* ref) {
		if (!ref) return false;
		auto base = ref->GetBaseObject();
		if (!base) return false;
        auto formid = base->GetFormID();
        for (const auto& src : sources) {
			if (src.formid == formid) return true;
		}
        return false;
	}


    void ActivateContainer(RE::TESObjectREFR* a_container) {
        Utilities::MsgBoxesNotifs::ShowMessageBox(
            std::to_string(a_container->GetFormID()) + " " + std::to_string(a_container->GetBaseObject()->GetFormID()),
            buttons,
            [this, a_container](unsigned int result) { this->MsgBoxCallback(result, a_container); });
    };

    void DeactivateContainer() {
		if (!current_container) return;
        RemoveAllItemsFromChest(true);
    }

};
