#include "_logger.hh"

#include <atomic>

namespace {
    // [ISO8583] 3.3 (Sicherheits-Audit, F3): Thread-sichere Globals.
    // - g_level: atomarisches Level (Hot-Pfad = ein atomares Load).
    // - g_external_logger: atomarer Zeiger (load/store mit acquire/release).
    //
    // Race-frei: Der Logger wird bei setLogger() atomar ausgetauscht.
    // Eine laufende Log-Nachricht kann noch den alten Logger erreichen -
    // das ist erlaubt (der Aufrufer garantiert die Lebensdauer des Loggers
    // bis zur Stilllegung der Bibliothek, s. ISOLog.hh).
    std::atomic<::TNG_NAMESPACE::log::ISOLogger*> g_external_logger{nullptr};
    std::atomic<::TNG_NAMESPACE::log::Level>      g_level{::TNG_NAMESPACE::log::Level::WARN};
}

namespace TNG_NAMESPACE::log {

    // [ISO8583] Öffentliche, exportierte Logger-Zugriffe (s. ISOLog.hh).
    // currentLogger() muss exportiert sein, weil die TNG_LOG_*-Makros auch
    // in Translation-Units AUSERHALLB der Bibliothek-DLL instanziiert werden
    // (Unit-Tests, die interne Headers inkludieren, oder Anwender, die die
    // Feld-Parser-Templates instantiieren). Damit verweist die Log-Funktion
    // über die DLL-Grenze korrekt auf den registrierten Logger.
    ISOLogger* currentLogger() { return g_external_logger.load(std::memory_order_acquire); }
    Level      getLevel() { return g_level.load(std::memory_order_acquire); }

    void setLevel(Level lvl) { g_level.store(lvl, std::memory_order_release); }

    void setLogger(ISOLogger* logger) {
        g_external_logger.store(logger, std::memory_order_release);
    }

    void setQuillLogger(void* /*quillLoggerPtr*/) {
        // setQuillLogger() ist durch das DLL-Singleton-Problem von Quill
        // nicht funktionsfähig wenn iso8583 als DLL gelinkt wird.
        // Verwende stattdessen setLogger() mit einer QuillBridge:
        //
        //   #include <iso8583/ISOLog.hh>  // enthält QuillBridge
        //   static iso8583::log::QuillBridge bridge(myQuillLogger);
        //   iso8583::log::setLogger(&bridge);
        //
        // QuillBridge ruft Quill-Makros im Kontext der tng.exe auf –
        // damit im korrekten Singleton, nicht im DLL-Singleton.
    }

} // namespace TNG_NAMESPACE::log
