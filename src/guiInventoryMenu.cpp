/*
Minetest-c55
Copyright (C) 2010 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#include "guiInventoryMenu.h"
#include "constants.h"
#include "gamedef.h"
#include "keycode.h"
#include "strfnd.h"
#include <IGUICheckBox.h>
#include <IGUIEditBox.h>
#include <IGUIButton.h>
#include <IGUIStaticText.h>
#include <IGUIFont.h>
#include "log.h"
#include "tile.h" // ITextureSource
#include "util/string.h"
#include "util/numeric.h"

void drawItemStack(video::IVideoDriver *driver,
		gui::IGUIFont *font,
		const ItemStack &item,
		const core::rect<s32> &rect,
		const core::rect<s32> *clip,
		IGameDef *gamedef)
{
	if(item.empty())
		return;
	
	const ItemDefinition &def = item.getDefinition(gamedef->idef());
	video::ITexture *texture = def.inventory_texture;

	// Draw the inventory texture
	if(texture != NULL)
	{
		const video::SColor color(255,255,255,255);
		const video::SColor colors[] = {color,color,color,color};
		driver->draw2DImage(texture, rect,
			core::rect<s32>(core::position2d<s32>(0,0),
			core::dimension2di(texture->getOriginalSize())),
			clip, colors, true);
	}

	if(def.type == ITEM_TOOL && item.wear != 0)
	{
		// Draw a progressbar
		float barheight = rect.getHeight()/16;
		float barpad_x = rect.getWidth()/16;
		float barpad_y = rect.getHeight()/16;
		core::rect<s32> progressrect(
			rect.UpperLeftCorner.X + barpad_x,
			rect.LowerRightCorner.Y - barpad_y - barheight,
			rect.LowerRightCorner.X - barpad_x,
			rect.LowerRightCorner.Y - barpad_y);

		// Shrink progressrect by amount of tool damage
		float wear = item.wear / 65535.0;
		int progressmid =
			wear * progressrect.UpperLeftCorner.X +
			(1-wear) * progressrect.LowerRightCorner.X;

		// Compute progressbar color
		//   wear = 0.0: green
		//   wear = 0.5: yellow
		//   wear = 1.0: red
		video::SColor color(255,255,255,255);
		int wear_i = MYMIN(floor(wear * 600), 511);
		wear_i = MYMIN(wear_i + 10, 511);
		if(wear_i <= 255)
			color.set(255, wear_i, 255, 0);
		else
			color.set(255, 255, 511-wear_i, 0);

		core::rect<s32> progressrect2 = progressrect;
		progressrect2.LowerRightCorner.X = progressmid;
		driver->draw2DRectangle(color, progressrect2, clip);

		color = video::SColor(255,0,0,0);
		progressrect2 = progressrect;
		progressrect2.UpperLeftCorner.X = progressmid;
		driver->draw2DRectangle(color, progressrect2, clip);
	}

	if(font != NULL && item.count >= 2)
	{
		// Get the item count as a string
		std::string text = itos(item.count);
		v2u32 dim = font->getDimension(narrow_to_wide(text).c_str());
		v2s32 sdim(dim.X,dim.Y);

		core::rect<s32> rect2(
			/*rect.UpperLeftCorner,
			core::dimension2d<u32>(rect.getWidth(), 15)*/
			rect.LowerRightCorner - sdim,
			sdim
		);

		video::SColor bgcolor(128,0,0,0);
		driver->draw2DRectangle(bgcolor, rect2, clip);

		video::SColor color(255,255,255,255);
		font->draw(text.c_str(), rect2, color, false, false, clip);
	}
}

/*
	GUIInventoryMenu
*/

GUIInventoryMenu::GUIInventoryMenu(gui::IGUIEnvironment* env,
		gui::IGUIElement* parent, s32 id,
		IMenuManager *menumgr,
		InventoryManager *invmgr,
		IGameDef *gamedef
):
	GUIModalMenu(env, parent, id, menumgr),
	m_invmgr(invmgr),
	m_gamedef(gamedef),
	m_form_src(NULL),
	m_selected_item(NULL),
	m_selected_amount(0),
	m_selected_dragging(false),
	m_tooltip_element(NULL)
{
}

GUIInventoryMenu::~GUIInventoryMenu()
{
	removeChildren();

	delete m_selected_item;
	delete m_form_src;
}

void GUIInventoryMenu::removeChildren()
{
	const core::list<gui::IGUIElement*> &children = getChildren();
	core::list<gui::IGUIElement*> children_copy;
	for(core::list<gui::IGUIElement*>::ConstIterator
			i = children.begin(); i != children.end(); i++)
	{
		children_copy.push_back(*i);
	}
	for(core::list<gui::IGUIElement*>::Iterator
			i = children_copy.begin();
			i != children_copy.end(); i++)
	{
		(*i)->remove();
	}
	/*{
		gui::IGUIElement *e = getElementFromId(256);
		if(e != NULL)
			e->remove();
	}*/
	if(m_tooltip_element)
	{
		m_tooltip_element->remove();
		m_tooltip_element = NULL;
	}
}

void GUIInventoryMenu::regenerateGui(v2u32 screensize)
{
	// Remove children
	removeChildren();
	
	v2s32 size(100,100);
	s32 helptext_h = 15;
	core::rect<s32> rect;
	v2s32 basepos = getBasePos();
	
	/* Convert m_init_draw_spec to m_inventorylists */
	
	m_inventorylists.clear();
	m_images.clear();

	Strfnd f(m_formspec_string);
	while(f.atend() == false)
	{
		std::string type = trim(f.next("["));
		if(type == "invsize")
		{
			v2f invsize;
			invsize.X = stof(f.next(","));
			invsize.Y = stof(f.next(";"));
			infostream<<"invsize ("<<invsize.X<<","<<invsize.Y<<")"<<std::endl;
			f.next("]");

			padding = v2s32(screensize.Y/40, screensize.Y/40);
			spacing = v2s32(screensize.Y/12, screensize.Y/13);
			imgsize = v2s32(screensize.Y/15, screensize.Y/15);
			size = v2s32(
				padding.X*2+spacing.X*(invsize.X-1.0)+imgsize.X,
				padding.Y*2+spacing.Y*(invsize.Y-1.0)+imgsize.Y + (helptext_h-5)
			);
			rect = core::rect<s32>(
					screensize.X/2 - size.X/2,
					screensize.Y/2 - size.Y/2,
					screensize.X/2 + size.X/2,
					screensize.Y/2 + size.Y/2
			);
			DesiredRect = rect;
			recalculateAbsolutePosition(false);
			basepos = getBasePos();
		}
		else if(type == "list")
		{
			std::string name = f.next(";");
			InventoryLocation loc;
			if(name == "context" || name == "current_name")
				loc = m_current_inventory_location;
			else
				loc.deSerialize(name);
			std::string listname = f.next(";");
			v2s32 pos = basepos;
			pos.X += stof(f.next(",")) * (float)spacing.X;
			pos.Y += stof(f.next(";")) * (float)spacing.Y;
			v2s32 geom;
			geom.X = stoi(f.next(","));
			geom.Y = stoi(f.next(";"));
			infostream<<"list inv="<<name<<", listname="<<listname
					<<", pos=("<<pos.X<<","<<pos.Y<<")"
					<<", geom=("<<geom.X<<","<<geom.Y<<")"
					<<std::endl;
			f.next("]");
			m_inventorylists.push_back(ListDrawSpec(loc, listname, pos, geom));
		}
		else if(type == "image")
		{
			v2s32 pos = basepos;
			pos.X += stof(f.next(",")) * (float)spacing.X;
			pos.Y += stof(f.next(";")) * (float)spacing.Y;
			v2s32 geom;
			geom.X = stof(f.next(",")) * (float)imgsize.X;
			geom.Y = stof(f.next(";")) * (float)imgsize.Y;
			std::string name = f.next("]");
			infostream<<"image name="<<name
					<<", pos=("<<pos.X<<","<<pos.Y<<")"
					<<", geom=("<<geom.X<<","<<geom.Y<<")"
					<<std::endl;
			m_images.push_back(ImageDrawSpec(name, pos, geom));
		}
		else
		{
			// Ignore others
			std::string ts = f.next("]");
			infostream<<"Unknown DrawSpec: type="<<type<<", data=\""<<ts<<"\""
					<<std::endl;
		}
	}

	// Add children
	{
		core::rect<s32> rect(0, 0, size.X-padding.X*2, helptext_h);
		rect = rect + v2s32(size.X/2 - rect.getWidth()/2,
				size.Y-rect.getHeight()-5);
		const wchar_t *text =
		L"Left click: Move all items, Right click: Move single item";
		Environment->addStaticText(text, rect, false, true, this, 256);

		// Add tooltip
		// Note: parent != this so that the tooltip isn't clipped by the menu rectangle
		m_tooltip_element = Environment->addStaticText(L"",core::rect<s32>(0,0,110,18));
		m_tooltip_element->enableOverrideColor(true);
		m_tooltip_element->setBackgroundColor(video::SColor(255,110,130,60));
		m_tooltip_element->setDrawBackground(true);
		m_tooltip_element->setDrawBorder(true);
		m_tooltip_element->setOverrideColor(video::SColor(255,255,255,255));
		m_tooltip_element->setTextAlignment(gui::EGUIA_CENTER, gui::EGUIA_CENTER);
		m_tooltip_element->setWordWrap(false);
	}
}

GUIInventoryMenu::ItemSpec GUIInventoryMenu::getItemAtPos(v2s32 p) const
{
	core::rect<s32> imgrect(0,0,imgsize.X,imgsize.Y);
	
	for(u32 i=0; i<m_inventorylists.size(); i++)
	{
		const ListDrawSpec &s = m_inventorylists[i];

		for(s32 i=0; i<s.geom.X*s.geom.Y; i++)
		{
			s32 x = (i%s.geom.X) * spacing.X;
			s32 y = (i/s.geom.X) * spacing.Y;
			v2s32 p0(x,y);
			core::rect<s32> rect = imgrect + s.pos + p0;
			if(rect.isPointInside(p))
			{
				return ItemSpec(s.inventoryloc, s.listname, i);
			}
		}
	}

	return ItemSpec(InventoryLocation(), "", -1);
}

void GUIInventoryMenu::drawList(const ListDrawSpec &s, int phase)
{
	video::IVideoDriver* driver = Environment->getVideoDriver();

	// Get font
	gui::IGUIFont *font = NULL;
	gui::IGUISkin* skin = Environment->getSkin();
	if (skin)
		font = skin->getFont();
	
	Inventory *inv = m_invmgr->getInventory(s.inventoryloc);
	if(!inv){
		infostream<<"GUIInventoryMenu::drawList(): WARNING: "
				<<"The inventory location "
				<<"\""<<s.inventoryloc.dump()<<"\" doesn't exist"
				<<std::endl;
		return;
	}
	InventoryList *ilist = inv->getList(s.listname);
	if(!ilist){
		infostream<<"GUIInventoryMenu::drawList(): WARNING: "
				<<"The inventory list \""<<s.listname<<"\" @ \""
				<<s.inventoryloc.dump()<<"\" doesn't exist"
				<<std::endl;
		return;
	}
	
	core::rect<s32> imgrect(0,0,imgsize.X,imgsize.Y);
	
	for(s32 i=0; i<s.geom.X*s.geom.Y; i++)
	{
		s32 x = (i%s.geom.X) * spacing.X;
		s32 y = (i/s.geom.X) * spacing.Y;
		v2s32 p(x,y);
		core::rect<s32> rect = imgrect + s.pos + p;
		ItemStack item;
		if(ilist)
			item = ilist->getItem(i);

		bool selected = m_selected_item
			&& m_invmgr->getInventory(m_selected_item->inventoryloc) == inv
			&& m_selected_item->listname == s.listname
			&& m_selected_item->i == i;
		bool hovering = rect.isPointInside(m_pointer);

		if(phase == 0)
		{
			if(hovering && m_selected_item)
			{
				video::SColor bgcolor(255,192,192,192);
				driver->draw2DRectangle(bgcolor, rect, &AbsoluteClippingRect);
			}
			else
			{
				video::SColor bgcolor(255,128,128,128);
				driver->draw2DRectangle(bgcolor, rect, &AbsoluteClippingRect);
			}
		}

		if(phase == 1)
		{
			// Draw item stack
			if(selected)
			{
				item.takeItem(m_selected_amount);
			}
			if(!item.empty())
			{
				drawItemStack(driver, font, item,
						rect, &AbsoluteClippingRect, m_gamedef);
			}

			// Draw tooltip
			std::string tooltip_text = "";
			if(hovering && !m_selected_item)
				tooltip_text = item.getDefinition(m_gamedef->idef()).description;
			if(tooltip_text != "")
			{
				m_tooltip_element->setVisible(true);
				this->bringToFront(m_tooltip_element);
				m_tooltip_element->setText(narrow_to_wide(tooltip_text).c_str());
				s32 tooltip_x = m_pointer.X + 15;
				s32 tooltip_y = m_pointer.Y + 15;
				s32 tooltip_width = m_tooltip_element->getTextWidth() + 15;
				s32 tooltip_height = m_tooltip_element->getTextHeight() + 5;
				m_tooltip_element->setRelativePosition(core::rect<s32>(
						core::position2d<s32>(tooltip_x, tooltip_y),
						core::dimension2d<s32>(tooltip_width, tooltip_height)));
			}
		}
	}
}

void GUIInventoryMenu::drawSelectedItem()
{
	if(!m_selected_item)
		return;

	video::IVideoDriver* driver = Environment->getVideoDriver();

	// Get font
	gui::IGUIFont *font = NULL;
	gui::IGUISkin* skin = Environment->getSkin();
	if (skin)
		font = skin->getFont();
	
	Inventory *inv = m_invmgr->getInventory(m_selected_item->inventoryloc);
	assert(inv);
	InventoryList *list = inv->getList(m_selected_item->listname);
	assert(list);
	ItemStack stack = list->getItem(m_selected_item->i);
	stack.count = m_selected_amount;

	core::rect<s32> imgrect(0,0,imgsize.X,imgsize.Y);
	core::rect<s32> rect = imgrect + (m_pointer - imgrect.getCenter());
	drawItemStack(driver, font, stack, rect, NULL, m_gamedef);
}

void GUIInventoryMenu::drawMenu()
{
	if(m_form_src){
		std::string newform = m_form_src->getForm();
		if(newform != m_formspec_string){
			m_formspec_string = newform;
			regenerateGui(m_screensize_old);
		}
	}

	updateSelectedItem();

	gui::IGUISkin* skin = Environment->getSkin();
	if (!skin)
		return;
	video::IVideoDriver* driver = Environment->getVideoDriver();
	
	video::SColor bgcolor(140,0,0,0);
	driver->draw2DRectangle(bgcolor, AbsoluteRect, &AbsoluteClippingRect);

	m_tooltip_element->setVisible(false);

	/*
		Draw items
		Phase 0: Item slot rectangles
		Phase 1: Item images; prepare tooltip
	*/
	
	for(int phase=0; phase<=1; phase++)
	for(u32 i=0; i<m_inventorylists.size(); i++)
	{
		drawList(m_inventorylists[i], phase);
	}

	for(u32 i=0; i<m_images.size(); i++)
	{
		const ImageDrawSpec &spec = m_images[i];
		video::ITexture *texture =
				m_gamedef->tsrc()->getTextureRaw(spec.name);
		// Image size on screen
		core::rect<s32> imgrect(0, 0, spec.geom.X, spec.geom.Y);
		// Image rectangle on screen
		core::rect<s32> rect = imgrect + spec.pos;
		const video::SColor color(255,255,255,255);
		const video::SColor colors[] = {color,color,color,color};
		driver->draw2DImage(texture, rect,
			core::rect<s32>(core::position2d<s32>(0,0),
					core::dimension2di(texture->getOriginalSize())),
			NULL/*&AbsoluteClippingRect*/, colors, true);
	}

	/*
		Draw dragged item stack
	*/
	drawSelectedItem();

	/*
		Call base class
	*/
	gui::IGUIElement::draw();
}

void GUIInventoryMenu::updateSelectedItem()
{
	// If the selected stack has become empty for some reason, deselect it.
	// If the selected stack has become smaller, adjust m_selected_amount.
	if(m_selected_item)
	{
		bool selection_valid = false;
		if(m_selected_item->isValid())
		{
			Inventory *inv = m_invmgr->getInventory(m_selected_item->inventoryloc);
			if(inv)
			{
				InventoryList *list = inv->getList(m_selected_item->listname);
				if(list && (u32) m_selected_item->i < list->getSize())
				{
					ItemStack stack = list->getItem(m_selected_item->i);
					if(m_selected_amount > stack.count)
						m_selected_amount = stack.count;
					if(!stack.empty())
						selection_valid = true;
				}
			}
		}
		if(!selection_valid)
		{
			delete m_selected_item;
			m_selected_item = NULL;
			m_selected_amount = 0;
			m_selected_dragging = false;
		}
	}

	// If craftresult is nonempty and nothing else is selected, select it now.
	if(!m_selected_item)
	{
		for(u32 i=0; i<m_inventorylists.size(); i++)
		{
			const ListDrawSpec &s = m_inventorylists[i];
			if(s.listname == "craftpreview")
			{
				Inventory *inv = m_invmgr->getInventory(s.inventoryloc);
				InventoryList *list = inv->getList("craftresult");
				if(list && list->getSize() >= 1 && !list->getItem(0).empty())
				{
					m_selected_item = new ItemSpec;
					m_selected_item->inventoryloc = s.inventoryloc;
					m_selected_item->listname = "craftresult";
					m_selected_item->i = 0;
					m_selected_amount = 0;
					m_selected_dragging = false;
					break;
				}
			}
		}
	}

	// If craftresult is selected, keep the whole stack selected
	if(m_selected_item && m_selected_item->listname == "craftresult")
	{
		Inventory *inv = m_invmgr->getInventory(m_selected_item->inventoryloc);
		assert(inv);
		InventoryList *list = inv->getList(m_selected_item->listname);
		assert(list);
		m_selected_amount = list->getItem(m_selected_item->i).count;
	}
}

bool GUIInventoryMenu::OnEvent(const SEvent& event)
{
	if(event.EventType==EET_KEY_INPUT_EVENT)
	{
		KeyPress kp(event.KeyInput);
		if (event.KeyInput.PressedDown && (kp == EscapeKey ||
			kp == getKeySetting("keymap_inventory")))
		{
			quitMenu();
			return true;
		}
	}
	if(event.EventType==EET_MOUSE_INPUT_EVENT
			&& event.MouseInput.Event == EMIE_MOUSE_MOVED)
	{
		// Mouse moved
		m_pointer = v2s32(event.MouseInput.X, event.MouseInput.Y);
	}
	if(event.EventType==EET_MOUSE_INPUT_EVENT
			&& event.MouseInput.Event != EMIE_MOUSE_MOVED)
	{
		// Mouse event other than movement

		v2s32 p(event.MouseInput.X, event.MouseInput.Y);
		m_pointer = p;

		// Get selected item and hovered/clicked item (s)

		updateSelectedItem();
		ItemSpec s = getItemAtPos(p);

		Inventory *inv_selected = NULL;
		Inventory *inv_s = NULL;

		if(m_selected_item)
		{
			inv_selected = m_invmgr->getInventory(m_selected_item->inventoryloc);
			assert(inv_selected);
			assert(inv_selected->getList(m_selected_item->listname) != NULL);
		}

		u32 s_count = 0;

		if(s.isValid())
		do{ // breakable
			inv_s = m_invmgr->getInventory(s.inventoryloc);

			if(!inv_s){
				errorstream<<"InventoryMenu: The selected inventory location "
						<<"\""<<s.inventoryloc.dump()<<"\" doesn't exist"
						<<std::endl;
				s.i = -1;  // make it invalid again
				break;
			}

			InventoryList *list = inv_s->getList(s.listname);
			if(list == NULL){
				errorstream<<"InventoryMenu: The selected inventory list \""
						<<s.listname<<"\" does not exist"<<std::endl;
				s.i = -1;  // make it invalid again
				break;
			}

			if((u32)s.i >= list->getSize()){
				errorstream<<"InventoryMenu: The selected inventory list \""
						<<s.listname<<"\" is too small (i="<<s.i<<", size="
						<<list->getSize()<<")"<<std::endl;
				s.i = -1;  // make it invalid again
				break;
			}

			s_count = list->getItem(s.i).count;
		}while(0);

		bool identical = (m_selected_item != NULL) && s.isValid() &&
			(inv_selected == inv_s) &&
			(m_selected_item->listname == s.listname) &&
			(m_selected_item->i == s.i);

		// buttons: 0 = left, 1 = right, 2 = middle
		// up/down: 0 = down (press), 1 = up (release), 2 = unknown event
		int button = 0;
		int updown = 2;
		if(event.MouseInput.Event == EMIE_LMOUSE_PRESSED_DOWN)
			{ button = 0; updown = 0; }
		else if(event.MouseInput.Event == EMIE_RMOUSE_PRESSED_DOWN)
			{ button = 1; updown = 0; }
		else if(event.MouseInput.Event == EMIE_MMOUSE_PRESSED_DOWN)
			{ button = 2; updown = 0; }
		else if(event.MouseInput.Event == EMIE_LMOUSE_LEFT_UP)
			{ button = 0; updown = 1; }
		else if(event.MouseInput.Event == EMIE_RMOUSE_LEFT_UP)
			{ button = 1; updown = 1; }
		else if(event.MouseInput.Event == EMIE_MMOUSE_LEFT_UP)
			{ button = 2; updown = 1; }

		// Set this number to a positive value to generate a move action
		// from m_selected_item to s.
		u32 move_amount = 0;

		// Set this number to a positive value to generate a drop action
		// from m_selected_item.
		u32 drop_amount = 0;

		// Set this number to a positive value to generate a craft action at s.
		u32 craft_amount = 0;

		if(updown == 0)
		{
			// Some mouse button has been pressed

			//infostream<<"Mouse button "<<button<<" pressed at p=("
			//	<<p.X<<","<<p.Y<<")"<<std::endl;

			m_selected_dragging = false;

			if(s.isValid() && s.listname == "craftpreview")
			{
				// Craft preview has been clicked: craft
				craft_amount = (button == 2 ? 10 : 1);
			}
			else if(m_selected_item == NULL)
			{
				if(s_count != 0)
				{
					// Non-empty stack has been clicked: select it
					m_selected_item = new ItemSpec(s);

					if(button == 1)  // right
						m_selected_amount = (s_count + 1) / 2;
					else if(button == 2)  // middle
						m_selected_amount = MYMIN(s_count, 10);
					else  // left
						m_selected_amount = s_count;

					m_selected_dragging = true;
				}
			}
			else  // m_selected_item != NULL
			{
				assert(m_selected_amount >= 1);

				if(s.isValid())
				{
					// Clicked a slot: move
					if(button == 1)  // right
						move_amount = 1;
					else if(button == 2)  // middle
						move_amount = MYMIN(m_selected_amount, 10);
					else  // left
						move_amount = m_selected_amount;

					if(identical)
					{
						if(move_amount >= m_selected_amount)
							m_selected_amount = 0;
						else
							m_selected_amount -= move_amount;
						move_amount = 0;
					}
				}
				else if(getAbsoluteClippingRect().isPointInside(m_pointer))
				{
					// Clicked somewhere else: deselect
					m_selected_amount = 0;
				}
				else
				{
					// Clicked outside of the window: drop
					if(button == 1)  // right
						drop_amount = 1;
					else if(button == 2)  // middle
						drop_amount = MYMIN(m_selected_amount, 10);
					else  // left
						drop_amount = m_selected_amount;
				}
			}
		}
		else if(updown == 1)
		{
			// Some mouse button has been released

			//infostream<<"Mouse button "<<button<<" released at p=("
			//	<<p.X<<","<<p.Y<<")"<<std::endl;

			if(m_selected_item != NULL && m_selected_dragging && s.isValid())
			{
				if(!identical)
				{
					// Dragged to different slot: move all selected
					move_amount = m_selected_amount;
				}
			}
			else if(m_selected_item != NULL && m_selected_dragging &&
				!(getAbsoluteClippingRect().isPointInside(m_pointer)))
			{
				// Dragged outside of window: drop all selected
				drop_amount = m_selected_amount;
			}

			m_selected_dragging = false;
		}

		// Possibly send inventory action to server
		if(move_amount > 0)
		{
			// Send IACTION_MOVE

			assert(m_selected_item && m_selected_item->isValid());
			assert(s.isValid());

			assert(inv_selected && inv_s);
			InventoryList *list_from = inv_selected->getList(m_selected_item->listname);
			InventoryList *list_to = inv_s->getList(s.listname);
			assert(list_from && list_to);
			ItemStack stack_from = list_from->getItem(m_selected_item->i);
			ItemStack stack_to = list_to->getItem(s.i);

			// Check how many items can be moved
			move_amount = stack_from.count = MYMIN(move_amount, stack_from.count);
			ItemStack leftover = stack_to.addItem(stack_from, m_gamedef->idef());
			if(leftover.count == stack_from.count)
			{
				// Swap the stacks
				m_selected_amount -= stack_to.count;
			}
			else if(leftover.empty())
			{
				// Item fits
				m_selected_amount -= move_amount;
			}
			else
			{
				// Item only fits partially
				move_amount -= leftover.count;
				m_selected_amount -= move_amount;
			}

			infostream<<"Handing IACTION_MOVE to manager"<<std::endl;
			IMoveAction *a = new IMoveAction();
			a->count = move_amount;
			a->from_inv = m_selected_item->inventoryloc;
			a->from_list = m_selected_item->listname;
			a->from_i = m_selected_item->i;
			a->to_inv = s.inventoryloc;
			a->to_list = s.listname;
			a->to_i = s.i;
			m_invmgr->inventoryAction(a);
		}
		else if(drop_amount > 0)
		{
			// Send IACTION_DROP

			assert(m_selected_item && m_selected_item->isValid());
			assert(inv_selected);
			InventoryList *list_from = inv_selected->getList(m_selected_item->listname);
			assert(list_from);
			ItemStack stack_from = list_from->getItem(m_selected_item->i);

			// Check how many items can be dropped
			drop_amount = stack_from.count = MYMIN(drop_amount, stack_from.count);
			assert(drop_amount > 0 && drop_amount <= m_selected_amount);
			m_selected_amount -= drop_amount;

			infostream<<"Handing IACTION_DROP to manager"<<std::endl;
			IDropAction *a = new IDropAction();
			a->count = drop_amount;
			a->from_inv = m_selected_item->inventoryloc;
			a->from_list = m_selected_item->listname;
			a->from_i = m_selected_item->i;
			m_invmgr->inventoryAction(a);
		}
		else if(craft_amount > 0)
		{
			// Send IACTION_CRAFT

			assert(s.isValid());
			assert(inv_s);

			infostream<<"Handing IACTION_CRAFT to manager"<<std::endl;
			ICraftAction *a = new ICraftAction();
			a->count = craft_amount;
			a->craft_inv = s.inventoryloc;
			m_invmgr->inventoryAction(a);
		}

		// If m_selected_amount has been decreased to zero, deselect
		if(m_selected_amount == 0)
		{
			delete m_selected_item;
			m_selected_item = NULL;
			m_selected_amount = 0;
			m_selected_dragging = false;
		}
	}
	if(event.EventType==EET_GUI_EVENT)
	{
		if(event.GUIEvent.EventType==gui::EGET_ELEMENT_FOCUS_LOST
				&& isVisible())
		{
			if(!canTakeFocus(event.GUIEvent.Element))
			{
				infostream<<"GUIInventoryMenu: Not allowing focus change."
						<<std::endl;
				// Returning true disables focus change
				return true;
			}
		}
		if(event.GUIEvent.EventType==gui::EGET_BUTTON_CLICKED)
		{
			/*switch(event.GUIEvent.Caller->getID())
			{
			case 256: // continue
				setVisible(false);
				break;
			case 257: // exit
				dev->closeDevice();
				break;
			}*/
		}
	}

	return Parent ? Parent->OnEvent(event) : false;
}

