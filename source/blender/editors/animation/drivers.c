/*
 * ***** BEGIN GPL LICENSE BLOCK *****
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2009 Blender Foundation, Joshua Leung
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Joshua Leung (full recode)
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/animation/drivers.c
 *  \ingroup edanimation
 */

 
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "DNA_anim_types.h"
#include "DNA_object_types.h"
#include "DNA_material_types.h"
#include "DNA_texture_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "BKE_animsys.h"
#include "BKE_fcurve.h"
#include "BKE_context.h"
#include "BKE_report.h"
#include "BKE_material.h"
#include "BKE_texture.h"

#include "ED_keyframing.h"

#include "UI_interface.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "anim_intern.h"

/* called by WM */
void free_anim_drivers_copybuf(void);

/* ************************************************** */
/* Animation Data Validation */

/* Get (or add relevant data to be able to do so) F-Curve from the driver stack, 
 * for the given Animation Data block. This assumes that all the destinations are valid.
 *	
 *	- add:	0 - don't add anything if not found, 
 *			1 - add new Driver FCurve (with keyframes for visual tweaking),
 *			2 - add new Driver FCurve (with generator, for script backwards compatibility)
 *			-1 - add new Driver FCurve without driver stuff (for pasting)
 */
FCurve *verify_driver_fcurve(ID *id, const char rna_path[], const int array_index, short add)
{
	AnimData *adt;
	FCurve *fcu;
	
	/* sanity checks */
	if (ELEM(NULL, id, rna_path))
		return NULL;
	
	/* init animdata if none available yet */
	adt = BKE_animdata_from_id(id);
	if ((adt == NULL) && (add))
		adt = BKE_id_add_animdata(id);
	if (adt == NULL) {
		/* if still none (as not allowed to add, or ID doesn't have animdata for some reason) */
		return NULL;
	}
		
	/* try to find f-curve matching for this setting 
	 *	- add if not found and allowed to add one
	 *		TODO: add auto-grouping support? how this works will need to be resolved
	 */
	fcu = list_find_fcurve(&adt->drivers, rna_path, array_index);
	
	if ((fcu == NULL) && (add)) {
		/* use default settings to make a F-Curve */
		fcu = MEM_callocN(sizeof(FCurve), "FCurve");
		
		fcu->flag = (FCURVE_VISIBLE | FCURVE_SELECTED);
		
		/* store path - make copy, and store that */
		fcu->rna_path = BLI_strdup(rna_path);
		fcu->array_index = array_index;
		
		/* if add is negative, don't init this data yet, since it will be filled in by the pasted driver */
		if (add > 0) {
			BezTriple *bezt;
			size_t i;
			
			/* add some new driver data */
			fcu->driver = MEM_callocN(sizeof(ChannelDriver), "ChannelDriver");
			fcu->driver->flag |= DRIVER_FLAG_SHOWDEBUG;
			
			/* F-Modifier or Keyframes? */
			// FIXME: replace these magic numbers with defines
			if (add == 2) {
				/* Python API Backwards compatibility hack:
				 * Create FModifier so that old scripts won't break
				 * for now before 2.7 series -- (September 4, 2013)
				 */
				add_fmodifier(&fcu->modifiers, FMODIFIER_TYPE_GENERATOR);
			}
			else {
				/* add 2 keyframes so that user has something to work with 
				 * - These are configured to 0,0 and 1,1 to give a 1-1 mapping
				 *   which can be easily tweaked from there.
				 */
				insert_vert_fcurve(fcu, 0.0f, 0.0f, INSERTKEY_FAST);
				insert_vert_fcurve(fcu, 1.0f, 1.0f, INSERTKEY_FAST);
				
				/* configure this curve to extrapolate */
				for (i = 0, bezt = fcu->bezt;  (i < fcu->totvert) && bezt;  i++, bezt++) {
					bezt->h1 = bezt->h2 = HD_VECT;
				}
				
				fcu->extend = FCURVE_EXTRAPOLATE_LINEAR;
				calchandles_fcurve(fcu);
			}
		}
		
		/* just add F-Curve to end of driver list */
		BLI_addtail(&adt->drivers, fcu);
	}
	
	/* return the F-Curve */
	return fcu;
}

/* ************************************************** */
/* Driver Management API */

/* Main Driver Management API calls:
 *  Add a new driver for the specified property on the given ID block
 */
int ANIM_add_driver(ReportList *reports, ID *id, const char rna_path[], int array_index, short flag, int type)
{	
	PointerRNA id_ptr, ptr;
	PropertyRNA *prop;
	FCurve *fcu;
	int array_index_max;
	int done_tot = 0;
	
	/* validate pointer first - exit if failure */
	RNA_id_pointer_create(id, &id_ptr);
	if (RNA_path_resolve_property(&id_ptr, rna_path, &ptr, &prop) == false) {
		BKE_reportf(reports, RPT_ERROR, 
		            "Could not add driver, as RNA path is invalid for the given ID (ID = %s, path = %s)",
		            id->name, rna_path);
		return 0;
	}
	
	/* key entire array convenience method */
	if (array_index == -1) {
		array_index_max = RNA_property_array_length(&ptr, prop);
		array_index = 0;
	}
	else
		array_index_max = array_index;
	
	/* maximum index should be greater than the start index */
	if (array_index == array_index_max)
		array_index_max += 1;
	
	/* will only loop once unless the array index was -1 */
	for (; array_index < array_index_max; array_index++) {
		short add_mode = (flag & CREATEDRIVER_WITH_FMODIFIER) ? 2 : 1;
		
		/* create F-Curve with Driver */
		fcu = verify_driver_fcurve(id, rna_path, array_index, add_mode);
		
		if (fcu && fcu->driver) {
			ChannelDriver *driver = fcu->driver;
			
			/* set the type of the driver */
			driver->type = type;
			
			/* creating drivers for buttons will create the driver(s) with type 
			 * "scripted expression" so that their values won't be lost immediately,
			 * so here we copy those values over to the driver's expression
			 */
			if (type == DRIVER_TYPE_PYTHON) {
				PropertyType proptype = RNA_property_type(prop);
				int array = RNA_property_array_length(&ptr, prop);
				char *expression = driver->expression;
				int val, maxlen = sizeof(driver->expression);
				float fval;
				
				if (proptype == PROP_BOOLEAN) {
					if (!array) val = RNA_property_boolean_get(&ptr, prop);
					else val = RNA_property_boolean_get_index(&ptr, prop, array_index);
					
					BLI_strncpy(expression, (val) ? "True" : "False", maxlen);
				}
				else if (proptype == PROP_INT) {
					if (!array) val = RNA_property_int_get(&ptr, prop);
					else val = RNA_property_int_get_index(&ptr, prop, array_index);
					
					BLI_snprintf(expression, maxlen, "%d", val);
				}
				else if (proptype == PROP_FLOAT) {
					if (!array) fval = RNA_property_float_get(&ptr, prop);
					else fval = RNA_property_float_get_index(&ptr, prop, array_index);
					
					BLI_snprintf(expression, maxlen, "%.3f", fval);
				}
			}
			
			/* for easier setup of drivers from UI, a driver variable should be 
			 * added if flag is set (UI calls only)
			 */
			if (flag & CREATEDRIVER_WITH_DEFAULT_DVAR) {
				/* assume that users will mostly want this to be of type "Transform Channel" too,
				 * since this allows the easiest setting up of common rig components
				 */
				DriverVar *dvar = driver_add_new_variable(driver);
				driver_change_variable_type(dvar, DVAR_TYPE_TRANSFORM_CHAN);
			}
		}
		
		/* set the done status */
		done_tot += (fcu != NULL);
	}
	
	/* done */
	return done_tot;
}

/* Main Driver Management API calls:
 *  Remove the driver for the specified property on the given ID block (if available)
 */
bool ANIM_remove_driver(ReportList *UNUSED(reports), ID *id, const char rna_path[], int array_index, short UNUSED(flag))
{
	AnimData *adt;
	FCurve *fcu;
	bool success = false;
	
	/* we don't check the validity of the path here yet, but it should be ok... */
	adt = BKE_animdata_from_id(id);
	
	if (adt) {
		if (array_index == -1) {
			/* step through all drivers, removing all of those with the same base path */
			FCurve *fcu_iter = adt->drivers.first;
			
			while ((fcu = iter_step_fcurve(fcu_iter, rna_path)) != NULL) {
				/* store the next fcurve for looping  */
				fcu_iter = fcu->next;
				
				/* remove F-Curve from driver stack, then free it */
				BLI_remlink(&adt->drivers, fcu);
				free_fcurve(fcu);
				
				/* done successfully */
				success = true;
			}
		}
		else {
			/* find the matching driver and remove it only 
			 * Note: here is one of the places where we don't want new F-Curve + Driver added!
			 *      so 'add' var must be 0
			 */
			fcu = verify_driver_fcurve(id, rna_path, array_index, 0);
			if (fcu) {
				BLI_remlink(&adt->drivers, fcu);
				free_fcurve(fcu);
				
				success = true;
			}
		}
	}

	return success;
}

/* ************************************************** */
/* Driver Management API - Copy/Paste Drivers */

/* Copy/Paste Buffer for Driver Data... */
static FCurve *channeldriver_copypaste_buf = NULL;

/* This function frees any MEM_calloc'ed copy/paste buffer data */
// XXX find some header to put this in!
void free_anim_drivers_copybuf(void)
{
	/* free the buffer F-Curve if it exists, as if it were just another F-Curve */
	if (channeldriver_copypaste_buf)
		free_fcurve(channeldriver_copypaste_buf);
	channeldriver_copypaste_buf = NULL;
}

/* Checks if there is a driver in the copy/paste buffer */
bool ANIM_driver_can_paste(void)
{
	return (channeldriver_copypaste_buf != NULL);
}

/* ------------------- */

/* Main Driver Management API calls:
 *  Make a copy of the driver for the specified property on the given ID block
 */
bool ANIM_copy_driver(ReportList *reports, ID *id, const char rna_path[], int array_index, short UNUSED(flag))
{
	PointerRNA id_ptr, ptr;
	PropertyRNA *prop;
	FCurve *fcu;
	
	/* validate pointer first - exit if failure */
	RNA_id_pointer_create(id, &id_ptr);
	if (RNA_path_resolve_property(&id_ptr, rna_path, &ptr, &prop) == false) {
		BKE_reportf(reports, RPT_ERROR,
		            "Could not find driver to copy, as RNA path is invalid for the given ID (ID = %s, path = %s)",
		            id->name, rna_path);
		return 0;
	}
	
	/* try to get F-Curve with Driver */
	fcu = verify_driver_fcurve(id, rna_path, array_index, 0);
	
	/* clear copy/paste buffer first (for consistency with other copy/paste buffers) */
	free_anim_drivers_copybuf();
	
	/* copy this to the copy/paste buf if it exists */
	if (fcu && fcu->driver) {
		/* make copies of some info such as the rna_path, then clear this info from the F-Curve temporarily
		 * so that we don't end up wasting memory storing the path which won't get used ever...
		 */
		char *tmp_path = fcu->rna_path;
		fcu->rna_path = NULL;
		
		/* make a copy of the F-Curve with */
		channeldriver_copypaste_buf = copy_fcurve(fcu);
		
		/* restore the path */
		fcu->rna_path = tmp_path;
		
		/* copied... */
		return 1;
	}
	
	/* done */
	return 0;
}

/* Main Driver Management API calls:
 *  Add a new driver for the specified property on the given ID block or replace an existing one
 *	with the driver + driver-curve data from the buffer 
 */
bool ANIM_paste_driver(ReportList *reports, ID *id, const char rna_path[], int array_index, short UNUSED(flag))
{	
	PointerRNA id_ptr, ptr;
	PropertyRNA *prop;
	FCurve *fcu;
	
	/* validate pointer first - exit if failure */
	RNA_id_pointer_create(id, &id_ptr);
	if (RNA_path_resolve_property(&id_ptr, rna_path, &ptr, &prop) == false) {
		BKE_reportf(reports, RPT_ERROR,
		            "Could not paste driver, as RNA path is invalid for the given ID (ID = %s, path = %s)",
		            id->name, rna_path);
		return 0;
	}
	
	/* if the buffer is empty, cannot paste... */
	if (channeldriver_copypaste_buf == NULL) {
		BKE_report(reports, RPT_ERROR, "Paste driver: no driver to paste");
		return 0;
	}
	
	/* create Driver F-Curve, but without data which will be copied across... */
	fcu = verify_driver_fcurve(id, rna_path, array_index, -1);

	if (fcu) {
		/* copy across the curve data from the buffer curve 
		 * NOTE: this step needs care to not miss new settings
		 */
		/* keyframes/samples */
		fcu->bezt = MEM_dupallocN(channeldriver_copypaste_buf->bezt);
		fcu->fpt = MEM_dupallocN(channeldriver_copypaste_buf->fpt);
		fcu->totvert = channeldriver_copypaste_buf->totvert;
		
		/* modifiers */
		copy_fmodifiers(&fcu->modifiers, &channeldriver_copypaste_buf->modifiers);
		
		/* extrapolation mode */
		fcu->extend = channeldriver_copypaste_buf->extend;
			
		/* the 'juicy' stuff - the driver */
		fcu->driver = fcurve_copy_driver(channeldriver_copypaste_buf->driver);
	}
	
	/* done */
	return (fcu != NULL);
}

/* ************************************************** */
/* UI-Button Interface */

/* Add Driver Button Operator ------------------------ */

static int add_driver_button_exec(bContext *C, wmOperator *op)
{
	PointerRNA ptr = {{NULL}};
	PropertyRNA *prop = NULL;
	int success = 0;
	int index;
	const bool all = RNA_boolean_get(op->ptr, "all");
	
	/* try to create driver using property retrieved from UI */
	UI_context_active_but_prop_get(C, &ptr, &prop, &index);
	
	if (all)
		index = -1;
	
	if (ptr.id.data && ptr.data && prop && RNA_property_animateable(&ptr, prop)) {
		char *path = BKE_animdata_driver_path_hack(C, &ptr, prop, NULL);
		short flags = CREATEDRIVER_WITH_DEFAULT_DVAR;
		
		if (path) {
			success += ANIM_add_driver(op->reports, ptr.id.data, path, index, flags, DRIVER_TYPE_PYTHON);
			
			MEM_freeN(path);
		}
	}
	
	if (success) {
		/* send updates */
		UI_context_update_anim_flag(C);
		
		WM_event_add_notifier(C, NC_ANIMATION | ND_FCURVES_ORDER, NULL); // XXX
	}
	
	return (success) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_driver_button_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Driver";
	ot->idname = "ANIM_OT_driver_button_add";
	ot->description = "Add driver(s) for the property(s) connected represented by the highlighted button";
	
	/* callbacks */
	ot->exec = add_driver_button_exec; 
	//op->poll = ??? // TODO: need to have some animatable property to do this
	
	/* flags */
	ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

	/* properties */
	RNA_def_boolean(ot->srna, "all", 1, "All", "Create drivers for all elements of the array");
}

/* Remove Driver Button Operator ------------------------ */

static int remove_driver_button_exec(bContext *C, wmOperator *op)
{
	PointerRNA ptr = {{NULL}};
	PropertyRNA *prop = NULL;
	short success = 0;
	int index;
	const bool all = RNA_boolean_get(op->ptr, "all");
	
	/* try to find driver using property retrieved from UI */
	UI_context_active_but_prop_get(C, &ptr, &prop, &index);
	
	if (all)
		index = -1;
	
	if (ptr.id.data && ptr.data && prop) {
		char *path = BKE_animdata_driver_path_hack(C, &ptr, prop, NULL);
		
		success = ANIM_remove_driver(op->reports, ptr.id.data, path, index, 0);
		MEM_freeN(path);
	}
	
	if (success) {
		/* send updates */
		UI_context_update_anim_flag(C);
		
		WM_event_add_notifier(C, NC_ANIMATION | ND_FCURVES_ORDER, NULL);  // XXX
	}
	
	return (success) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_driver_button_remove(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Remove Driver";
	ot->idname = "ANIM_OT_driver_button_remove";
	ot->description = "Remove the driver(s) for the property(s) connected represented by the highlighted button";
	
	/* callbacks */
	ot->exec = remove_driver_button_exec; 
	//op->poll = ??? // TODO: need to have some driver to be able to do this...
	
	/* flags */
	ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;

	/* properties */
	RNA_def_boolean(ot->srna, "all", 1, "All", "Delete drivers for all elements of the array");
}

/* Copy Driver Button Operator ------------------------ */

static int copy_driver_button_exec(bContext *C, wmOperator *op)
{
	PointerRNA ptr = {{NULL}};
	PropertyRNA *prop = NULL;
	short success = 0;
	int index;
	
	/* try to create driver using property retrieved from UI */
	UI_context_active_but_prop_get(C, &ptr, &prop, &index);
	
	if (ptr.id.data && ptr.data && prop && RNA_property_animateable(&ptr, prop)) {
		char *path = BKE_animdata_driver_path_hack(C, &ptr, prop, NULL);
		
		if (path) {
			/* only copy the driver for the button that this was involved for */
			success = ANIM_copy_driver(op->reports, ptr.id.data, path, index, 0);
			
			UI_context_update_anim_flag(C);
			
			MEM_freeN(path);
		}
	}
	
	/* since we're just copying, we don't really need to do anything else...*/
	return (success) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_copy_driver_button(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Copy Driver";
	ot->idname = "ANIM_OT_copy_driver_button";
	ot->description = "Copy the driver for the highlighted button";
	
	/* callbacks */
	ot->exec = copy_driver_button_exec; 
	//op->poll = ??? // TODO: need to have some driver to be able to do this...
	
	/* flags */
	ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* Paste Driver Button Operator ------------------------ */

static int paste_driver_button_exec(bContext *C, wmOperator *op)
{
	PointerRNA ptr = {{NULL}};
	PropertyRNA *prop = NULL;
	short success = 0;
	int index;
	
	/* try to create driver using property retrieved from UI */
	UI_context_active_but_prop_get(C, &ptr, &prop, &index);
	
	if (ptr.id.data && ptr.data && prop && RNA_property_animateable(&ptr, prop)) {
		char *path = BKE_animdata_driver_path_hack(C, &ptr, prop, NULL);
		
		if (path) {
			/* only copy the driver for the button that this was involved for */
			success = ANIM_paste_driver(op->reports, ptr.id.data, path, index, 0);
			
			UI_context_update_anim_flag(C);
			
			MEM_freeN(path);
		}
	}
	
	/* since we're just copying, we don't really need to do anything else...*/
	return (success) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_paste_driver_button(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Paste Driver";
	ot->idname = "ANIM_OT_paste_driver_button";
	ot->description = "Paste the driver in the copy/paste buffer for the highlighted button";
	
	/* callbacks */
	ot->exec = paste_driver_button_exec; 
	//op->poll = ??? // TODO: need to have some driver to be able to do this...
	
	/* flags */
	ot->flag = OPTYPE_UNDO | OPTYPE_INTERNAL;
}

/* ************************************************** */
