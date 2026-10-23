#pragma once
#include "assets.h"
void favoritesBegin();
void favoritesFace(int face);
bool favoritesCurrent();
bool favoritesToggle();
bool favoritesDirty();
bool favoritesPrompt();
bool favoritesLater();
int favoritesCount();
const char *favoritesError();
bool favoritesBuild(void (*progress)(const char *));
int favoritesGroupCount();
const char *favoritesDirectory();
void favoritesStatus();

const char *favoritesGroupName(int index);
