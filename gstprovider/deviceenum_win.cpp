/*
 * Copyright (C) 2008  Justin Karneges
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "deviceenum.h"

namespace DeviceEnum {

QList<Item> audioOutputItems(const QString &driver)
{
	QList<Item> out;

	if(driver == "directshow")
	{
		// hardcode a default output device
		Item i;
		i.type = Item::Audio;
		i.dir = Item::Output;
		i.name = "Default";
		i.driver = "directshow";
		i.id = QString(); // unspecified
		out += i;
	}

	return out;
}

QList<Item> audioInputItems(const QString &driver)
{
	QList<Item> out;

	if(driver == "directshow")
	{
		// hardcode a default input device
		Item i;
		i.type = Item::Audio;
		i.dir = Item::Input;
		i.name = "Default";
		i.driver = "directshow";
		i.id = QString(); // unspecified
		out += i;
	}

	return out;
}

QList<Item> videoInputItems(const QString &driver)
{
	QList<Item> out;

	if(driver == "directshow")
	{
		// hardcode a default input device
		Item i;
		i.type = Item::Video;
		i.dir = Item::Input;
		i.name = "Default";
		i.driver = "directshow";
		i.id = QString(); // unspecified
		out += i;
	}

	return out;
}

}