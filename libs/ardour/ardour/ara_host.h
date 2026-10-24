/*
 * MusicDAWM: ARA 2 hosting (WP9), for plug-ins such as Melodyne.
 *
 * Built only with --ara. The ARA SDK is Apache-2.0, so binaries that include
 * this are distributed under GPLv3 (DECISIONS.md D-005); this source file is
 * GPLv2 or later like the rest.
 *
 * Model (PHASE0.md section 3):
 *   one ARA document per session and ARA plug-in (factory)
 *   Session tempo map  -> musical context
 *   Track              -> region sequence
 *   a region's sources -> audio source (one per set of channel sources)
 *   ARA edits          -> audio modification, shared by the pieces of a split
 *                         region (its id is kept in the region's extra XML)
 *   AudioRegion        -> playback region, rendered by the ARA plug-in that is
 *                         the region's Region FX
 *
 * An ARA plug-in instance is put into ARA mode only when it is used as a
 * Region FX; as an ordinary insert it works as before (e.g. Melodyne's
 * "Transfer" mode).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#ifdef HAVE_ARA

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ardour/libardour_visibility.h"

namespace ARDOUR {

class AudioRegion;
class Plugin;
class Session;

class LIBARDOUR_API ARAHost
{
public:
	/* The document controller (an ARA::ARADocumentControllerRef) for the
	 * plug-in with this ARA factory (an ARA::ARAFactory const*) in this
	 * session; created on first use. */
	static void* document_controller (Session&, void const* ara_factory);

	/* A Region FX plug-in in ARA mode now renders this region, or (the
	 * plug-in given by its ARA::ARAPlugInExtensionInstance, or any) no
	 * longer does. */
	static void add_region (std::shared_ptr<AudioRegion>, std::shared_ptr<Plugin>);
	static void remove_region (AudioRegion const*, void const* plugin_extension);

	/* the region is going away */
	static void region_destroyed (AudioRegion const*);

	/* the plug-in instance (its ARA::ARAPlugInExtensionInstance) is going away */
	static void plugin_destroyed (void const* plugin_extension);

	/* a plug-in instance was bound to this document controller, or (after
	 * it was destroyed) no longer is: a document goes only when the
	 * session has gone and no instance is bound to it */
	static void plugin_bound (void* document_controller);
	static void plugin_released (void* document_controller);

	/* ARA documents of this session store their data next to the session
	 * file (called after the session was saved as `snapshot`). */
	static void save (Session&, std::string const& snapshot);

	/* Lets the plug-ins tell about their analysis; call regularly from the
	 * GUI thread. */
	static void idle ();

	/* For tests: the number of playback regions in the session's documents,
	 * and of samples the plug-ins read from the sources. */
	static size_t playback_regions (Session&);
	/* documents alive, including those waiting for their plug-ins after
	 * their session has gone (0 once everything is closed) */
	static size_t documents_alive ();
	static int64_t samples_read ();

	/* For tests: the notes the plug-in found in the region (content type
	 * "notes"), as (pitch, start seconds, duration seconds); false if the
	 * plug-in does not offer them (yet). */
	struct Note { double pitch; double start; double duration; };
	static bool region_notes (AudioRegion const*, std::vector<Note>&);
};

} /* namespace ARDOUR */

#endif /* HAVE_ARA */
