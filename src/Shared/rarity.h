#pragma once

enum class ERarity : int {
	Null = 0,
	Common,
	Unusual,
	Rare,
	Epic,
	Legendary,
	Mythic,
	Ultra,
	Super,
	Eternal,
	Unique,
	Primordial
};

inline int GetLevel(ERarity rarity)
{
	int level = static_cast<int>(rarity);
	if (level > static_cast<int>(ERarity::Unique))
		level--;
	if (level < 1)
		level = 1;
	return level;
}
