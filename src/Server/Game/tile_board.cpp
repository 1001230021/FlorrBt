#include "tile_board.h"

bool CTileBoard::InBounds(int row, int col) const
{
    return row >= 0 && col >= 0 && row < Height() && col < Width();
}