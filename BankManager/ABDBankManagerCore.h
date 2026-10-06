/**
 * ABD Bank Manager — C++ Core Module
 * Provides bank management functionality for JUCE plugins.
 */

#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "HardwareMidiPipe.h"

#include <functional>

namespace ABD::BankManager
{

struct Patch
{
    juce::String id;
    int index = 0;
    juce::String name;
    juce::String category;
    juce::String author;
    juce::StringArray tags;
    juce::String notes;
    juce::String originAddress;
    juce::String originModel;
    juce::String originBank;
    juce::MemoryBlock rawData;
    juce::StringArray hardwareIds;
    juce::var parameters;
    bool isFavorite   = false;
    int rating        = 0;
    int versionNumber = 1;
    juce::String previousVersionId;
    juce::String fingerprint;
    juce::String creationDate;
    juce::String modifiedDate;
    juce::String importSource;
    juce::String importDate;
};

struct Bank
{
    juce::String id;
    juce::String name;
    juce::String modelId;
    juce::StringArray hardwareIds;
    juce::String manufacturer;
    bool isFactory       = false;
    bool isLocked        = false;
    bool includeInBundle = false;
    juce::String source;
    juce::String imageUrl;
    juce::String description;
    juce::String bankAuthor;
    juce::String license;
    juce::StringArray tags;
    juce::String bankNotes;
    juce::String firmwareCompat;
    juce::String knownIssues;
    juce::String creationDate;
    juce::String modifiedDate;
    juce::Array<Patch> patches;
};

struct Library
{
    int version = 1;
    juce::String activeBankId;
    int activePresetIndex = 0;
    juce::String lastImportPath;
    juce::String lastExportPath;
    juce::Array<Bank> banks;
};

class BankManagerCore
{
public:
    static constexpr int valueTreeSchemaVersion = 1;

    using WebUIMessageHandler = std::function<void(const juce::String& event,
                                                   const juce::var& data)>;

    BankManagerCore();
    ~BankManagerCore() = default;

    // --- Library State ---
    void setLibrary(Library nextLibrary);
    const Library& getLibrary() const noexcept { return library; }

    // --- State Persistence (DAW session) ---
    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& vt);

    // --- Preset Management ---
    void selectPreset(int bankIndex, int patchIndex);
    int getCurrentBankIndex() const noexcept { return currentBankIndex; }
    int getCurrentPatchIndex() const noexcept { return currentPatchIndex; }

    // --- WebUI Bridge ---
    void setWebUIMessageHandler(WebUIMessageHandler handler);
    void handleWebUIMessage(const juce::String& type, const juce::var& data);
    void sendToWebUI(const juce::String& event, const juce::var& data);

    // --- Hardware MIDI (WebUI ↔ host transport) ---
    /** Transport-agnostic byte pipe; the host injects the real MIDI transport. */
    HardwareMidiPipe& getHardwareMidiPipe() noexcept { return hardwareMidiPipe; }

    // --- File Persistence (XML) ---
    bool saveToFile(const juce::File& file) const;
    bool loadFromFile(const juce::File& file);
    static juce::File getDefaultLibraryFile();

    // --- Auto-export (.abdlibrary) ---
    bool saveBackupToDisk(const juce::String& filename, const void* data, int dataSize);
    static juce::File getBackupDirectory();

private:
    static juce::var toWebUIState(const Library& state,
                                  int selectedBankIndex,
                                  int selectedPatchIndex);
    void loadWebUIState(const juce::var& data);
    void handleUpdateMetadata(const juce::var& data);
    void handleHardwareSend(const juce::var& data);

    bool hardwareListen = false;

    int findBankIndex(const juce::var& data) const;
    int findPatchIndex(const Bank& bank, const juce::var& data) const;

    Library library;
    int currentBankIndex  = 0;
    int currentPatchIndex = 0;
    WebUIMessageHandler webUIMessageHandler;
    HardwareMidiPipe hardwareMidiPipe;
};

} // namespace ABD::BankManager
