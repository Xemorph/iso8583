#pragma once

// [stdc++]
#include <memory>
#include <string>
// [tng/public]
#include <iso8583/config.h>
#include <iso8583/ISOSpec.hh>
#include <iso8583/detail/_interfaces.hh>
// [tng/internal]
#include "_parser.hh"

// Hinweis: SpecDecoder ist in ISOSpec.hh deklariert.
// _spec.hh existiert nur noch um die internen Parser-Header einzubinden
// die _spec.cc für die Implementierung braucht.
