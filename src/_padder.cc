#include "_padder.hh"

namespace TNG_NAMESPACE {

    /// Adds padding characters to the left side of the string `s` until it reaches length `n`.
    /// \param s The string to be padded.
    /// \param n The desired length of the string.
    /// \param p The padding character (default is a space).
    static void __pad_left(std::string& s, const size_t n, const char p) {
        if (n > s.size()) s.insert(0, n - s.size(), p);
    }
    /// Adds padding characters to the right side of the string `s` until it reaches length `n`.
    /// \param s The string to be padded.
    /// \param n The desired length of the string.
    /// \param p The padding character (default is a space).
    static void __pad_right(std::string& s, const size_t n, const char p) {
        if (n > s.size()) s.insert(s.size(), n - s.size(), p);
    }

    /// Adds padding characters to the right side of the string `s` until it reaches length `n`,
    /// but truncates the string if it exceeds that length.
    /// \param s The string to be padded.
    /// \param n The desired length of the string.
    /// \param p The padding character (default is a space).
    static void __pad_trunc_right(std::string& s, const size_t n, const char p) {
        if (n > s.size()) s.insert(s.size(), n - s.size(), p);
        else s.erase(n);
    }

    template < Padder e >
    void TNG_EXPORT pad(std::string& s, std::size_t mlen) {
        if constexpr (Padder::NONE == e)
            return;
        else if constexpr (Padder::LEFT_ZERO == e)
            __pad_left(s, mlen, '0');
        else if constexpr (Padder::RIGHT_SPACE == e)
            __pad_right(s, mlen, ' ');
        else if constexpr (Padder::RIGHT_T_SPACE == e)
            __pad_trunc_right(s, mlen, ' ');
        else
            static_assert(dependent_false<decltype(e)>::value, "Don't know how to pad");
    }

    // Explizite Instanziierungen der Template-Funktion
    template void TNG_EXPORT pad<Padder::NONE>(std::string&, std::size_t);
    template void TNG_EXPORT pad<Padder::LEFT_ZERO>(std::string&, std::size_t);
    template void TNG_EXPORT pad<Padder::RIGHT_SPACE>(std::string&, std::size_t);
    template void TNG_EXPORT pad<Padder::RIGHT_T_SPACE>(std::string&, std::size_t);

} // namespace TNG_NAMESPACE
