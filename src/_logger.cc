#include "_logger.hh"

namespace {
    ::TNG_NAMESPACE::log::ISOLogger* g_external_logger = nullptr;
    ::TNG_NAMESPACE::log::Level      g_level = ::TNG_NAMESPACE::log::Level::WARN;
}

namespace TNG_NAMESPACE::log {

    // [ISO8583] Öffentliche, exportierte Logger-Zugriffe (s. ISOLog.hh).
    // currentLogger() muss exportiert sein, weil die TNG_LOG_*-Makros auch
    // in Translation-Units AUSERHALLB der Bibliothek-DLL instanziiert werden
    // (Unit-Tests, die interne Headers inkludieren, oder Anwender, die die
    // Feld-Parser-Templates instantiieren). Damit verweist die Log-Funktion
    // über die DLL-Grenze korrekt auf den registrierten Logger.
    ISOLogger* currentLogger() { return g_external_logger; }
    Level      getLevel() { return g_level; }

    void setLevel(Level lvl) { g_level = lvl; }

    void setLogger(ISOLogger* logger) {
        g_external_logger = logger;
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
