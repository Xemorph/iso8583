#pragma once

// [tng]
#include "../config.h"
// [stdc++]
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
// [nonstd]
#include "extern/string_view.hpp"
// [nlohmann/json]
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace TNG_NAMESPACE {

    class TNG_EXPORT ISOComponentPtrBase {
    public:
        // Smart pointer concept
        using ISOComponentPtrBaseSmartPtr = std::shared_ptr<ISOComponentPtrBase>;

        // [Destructor]
        virtual ~ISOComponentPtrBase() = default;
        // Returns the key of 'Leaf'.
        // TNG_KEY_TYPE (int16_t) ist der verbindliche Schlüsseltyp der Bibliothek.
        // Vorher `int`: führte in ISOComponent<IntegerType,...> zu impliziten
        // Narrowing-Konvertierungen und Compiler-Warnungen.
        virtual TNG_KEY_TYPE key() const = 0;

        // Byte-Offset dieses Feldes im ursprünglichen Wire-Buffer.
        // Wird vom Parser nach dem Unparse befüllt.
        // Standardwert 0 bedeutet "nicht gesetzt" (MTI sitzt ebenfalls bei 0).
        // Für "unbekannt" kann std::numeric_limits<std::size_t>::max() verwendet werden.
        virtual std::size_t wire_offset() const noexcept = 0;
        virtual void        wire_offset(std::size_t offset) noexcept = 0;

        // Anzahl der Bytes, die dieses Feld im Wire-Buffer belegt (inkl. Längen-Prefix).
        // Zusammen mit wire_offset() kann das Feld im Rohdaten-Buffer exakt lokalisiert werden.
        virtual std::size_t wire_length() const noexcept = 0;
        virtual void        wire_length(std::size_t length) noexcept = 0;
        // Returns a human-readable value of 'Leaf'
        virtual std::string readable_value() const = 0;
        // Sets a description
        virtual void description(const nonstd::string_view& desc) = 0;
        // Returns a description
        virtual const nonstd::string_view description() const = 0;
        // Sets an value explanation
        virtual void explanation(const nonstd::string_view& expl) = 0;
        // Returns an explanation
        virtual const nonstd::string_view explanation() const = 0;
        // 
        virtual bool is_composite() const = 0;
        //
        virtual void print(bool nested) const = 0;
        //
        virtual json to_json() const = 0;
    };

    enum class ISOFieldParserType {
        UNUSED,
        EXCEPTIONAL,
        OPAQUE,
        BINARY,
        BITMAP,
        NESTED,
    };

    // Forward decleration
    class TNG_EXPORT ISOFieldParserPtrBase
    {
    public:
        // Smart Pointer concept
        using ISOFieldParserPtrBaseSmartPtr = std::shared_ptr<ISOFieldParserPtrBase>;

        // [Destructor]
        virtual ~ISOFieldParserPtrBase() = default;
        // Sets the data elements (DE) un/parser's description
        virtual void description(const nonstd::string_view& desc) = 0;
        // Returns the data elements (DE) un/parser's description
        virtual nonstd::string_view description() const = 0;

        // -------------------------
        // -          PSL          -
        // -------------------------

        //
        virtual const TNG_NAMESPACE::ISOFieldParserType type() const = 0;
        //
        virtual std::vector<uint8_t> parse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr) = 0;
        // Unparses the byte-image and stores the result into the user-defined ISOComponent
        virtual std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr, const std::vector<uint8_t>&, std::size_t) const = 0;
        //
        virtual ::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr create_component(TNG_KEY_TYPE) const = 0;
    };

    // Forward decleration
    class TNG_EXPORT ISOParserPtrBase
    {
    public:
        // Smart Pointer concept
        using ISOParserPtrBaseSmartPtr = std::shared_ptr<ISOParserPtrBase>;

        // [Destructor]
        virtual ~ISOParserPtrBase() = default;

        // -------------------------
        // -          PSL          -
        // -------------------------

        // Checks if the bitmap has to be emitted
        virtual bool emit_bitmap() noexcept = 0;

        // Haupteinstieg ohne Basis-Offset (Top-Level-Nachrichten, o=0)
        virtual std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr, const std::vector<uint8_t>&) = 0;

        // Einstieg mit Basis-Offset: wird vom nested-Pfad aufgerufen damit
        // Kind-Felder ihren wire_offset relativ zur Original-Nachricht erhalten.
        // base_offset = Byte-Position des Elternfeldes im Original-Buffer.
        // Default-Implementierung delegiert auf die Basis-Variante (kein Offset-Tracking).
        virtual std::size_t unparse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr c,
                                    const std::vector<uint8_t>& b,
                                    std::size_t base_offset) {
            return unparse(c, b);  // Default: Offset wird ignoriert
        }

        virtual std::vector<uint8_t> parse(::TNG_NAMESPACE::ISOComponentPtrBase::ISOComponentPtrBaseSmartPtr) = 0;

        // -------------------------
        // -        BEAUTIFY       -
        // -------------------------
    };

    class TNG_EXPORT ISOHeader {
    public:
        // Smart Pointer concept
        using ISOHeaderSmartPtr = std::shared_ptr<ISOHeader>;

        virtual ~ISOHeader() = default;

        // Packt den Header in ein Byte-Array
        virtual std::vector<uint8_t> pack() const = 0;

        // Entpackt den Header aus einem Byte-Array
        // Gibt die Anzahl der verbrauchten Bytes zurück
        virtual std::size_t unpack(const std::vector<uint8_t>& b) = 0;

        // Setzt die Zieladresse
        virtual void destination(const nonstd::string_view dst) = 0;

        // Gibt die Zieladresse zurück, falls vorhanden
        virtual std::optional<nonstd::string_view> destination() const = 0;

        // Setzt die Quellenadresse
        virtual void source(const nonstd::string_view src) = 0;

        // Gibt die Quellenadresse zurück, falls vorhanden
        virtual std::optional<nonstd::string_view> source() const = 0;

        // Gibt die Grösse des Headers in Bytes zurück
        virtual std::size_t size() const = 0;

        // Tauscht Quellen- und Zieladresse, falls vorhanden
        virtual void swapDirection() = 0;

        // Erstellt eine Kopie des Objekts
        virtual std::unique_ptr<ISOHeader> clone() const = 0;
    };
}
