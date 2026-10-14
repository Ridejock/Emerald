#pragma once

namespace Emerald {

// True when the process was started on a Steam Deck. Steam sets SteamDeck=1 for both Gaming
// Mode and Desktop Mode. There is no display or controller check behind it: a Deck that has not
// been tested here still reports the variable, and a desktop PC does not.
[[nodiscard]] bool IsSteamDeck();

} // namespace Emerald
