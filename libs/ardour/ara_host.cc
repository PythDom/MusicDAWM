/*
 * MusicDAWM: ARA 2 hosting (see ardour/ara_host.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_ARA

#include <atomic>
#include <cstring>
#include <map>
#include <set>

#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/id.h"
#include "pbd/xml++.h"

#include "temporal/tempo.h"

#include "ardour/ara_host.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/audiosource.h"
#include "ardour/playlist.h"
#include "ardour/plugin.h"
#include "ardour/session.h"
#include "ardour/session_handle.h"

#include "ARA_API/ARAInterface.h"
#include "ARA_Library/Dispatch/ARAHostDispatch.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace PBD;
using std::string;
using std::vector;

namespace {

struct Document;

/* one ARA audio source: the channel sources of a set of regions */
struct HostAudioSource {
	Document*                                   doc;
	string                                      key; /* persistent ID */
	string                                      name;
	vector<std::shared_ptr<ARDOUR::AudioSource>> channels;
	samplecnt_t                                 length;
	ARA::ARAAudioSourceRef                      ref;
	int                                         users;
};

struct HostAudioModification {
	Document*                     doc;
	string                        id; /* persistent ID, kept in the regions' extra XML */
	HostAudioSource*              source;
	ARA::ARAAudioModificationRef  ref;
	int                           users;
};

struct HostRegionSequence {
	Document*                  doc;
	PBD::ID                    route;
	ARA::ARARegionSequenceRef  ref;
	int                        users;
	string                     name;
};

struct HostPlaybackRegion {
	Document*                       doc;
	AudioRegion const*              region;
	void const*                     extension; /* the plug-in instance rendering it */
	std::weak_ptr<Plugin>           plugin;
	HostAudioModification*          modification;
	HostRegionSequence*             sequence;
	ARA::ARAPlaybackRegionRef       ref;
	string                          name;
	PBD::ScopedConnection           changed;
};

struct HostAudioReader {
	HostAudioSource* source;
	bool             use64;
	vector<float>    tmp;
};

struct HostContentReader {
	ARA::ARAContentType               type;
	vector<ARA::ARAContentTempoEntry> tempo;
	vector<ARA::ARAContentBarSignature> bars;
};

struct ArchiveData {
	vector<uint8_t> bytes;
	string          id;
};

} /* anonymous namespace */

ARA_MAP_HOST_REF (HostAudioSource, ARA::ARAAudioSourceHostRef)
ARA_MAP_HOST_REF (HostAudioModification, ARA::ARAAudioModificationHostRef)
ARA_MAP_HOST_REF (HostRegionSequence, ARA::ARARegionSequenceHostRef)
ARA_MAP_HOST_REF (HostPlaybackRegion, ARA::ARAPlaybackRegionHostRef)
ARA_MAP_HOST_REF (HostAudioReader, ARA::ARAAudioReaderHostRef)
ARA_MAP_HOST_REF (HostContentReader, ARA::ARAContentReaderHostRef)
ARA_MAP_HOST_REF (ArchiveData, ARA::ARAArchiveReaderHostRef, ARA::ARAArchiveWriterHostRef)
ARA_MAP_HOST_REF (Document, ARA::ARAMusicalContextHostRef)

namespace {

/* ---- the document, with the host side of the ARA interfaces -------------- */

struct Document
	: public ARA::Host::AudioAccessControllerInterface
	, public ARA::Host::ArchivingControllerInterface
	, public ARA::Host::ContentAccessControllerInterface
	, public ARA::Host::ModelUpdateControllerInterface
	, public ARA::Host::PlaybackControllerInterface
	, public SessionHandleRef
{
	ARA::ARAFactory const*                   factory;
	ARA::Host::DocumentControllerHostInstance host_instance;
	ARA::Host::DocumentController*           dc;
	ARA::ARAMusicalContextRef                musical_context;
	bool                                     editing;
	int                                      bound;    /* plug-in instances bound to it */
	bool                                     orphaned; /* the session has gone */

	std::map<string, HostAudioSource*>        sources;
	std::map<string, HostAudioModification*>  modifications;
	std::map<PBD::ID, HostRegionSequence*>    sequences;
	std::map<AudioRegion const*, HostPlaybackRegion*> regions;
	PBD::ScopedConnectionList                 connections;

	Document (Session& s, ARA::ARAFactory const* f);
	~Document ();

	void session_going_away () override;
	Session& sess () const { return _session; }

	void begin () { if (!editing) { dc->beginEditing (); editing = true; } }
	void end ()   { if (editing) { dc->endEditing (); editing = false; } }

	HostAudioSource*       audio_source (AudioRegion const&);
	HostAudioModification* modification (AudioRegion const&, HostAudioSource*);
	HostRegionSequence*    sequence (AudioRegion const&);
	void                   fill_region_properties (HostPlaybackRegion const&, ARA::ARAPlaybackRegionProperties&) const;
	void                   region_changed (HostPlaybackRegion*);
	void                   tempo_map_changed ();
	string                 archive_path (string const& snapshot) const;
	void                   save (string const& snapshot);
	void                   restore ();

	/* AudioAccessController */
	ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef, bool use64BitSamples) noexcept override;
	bool readAudioSamples (ARA::ARAAudioReaderHostRef, ARA::ARASamplePosition, ARA::ARASampleCount, void* const buffers[]) noexcept override;
	void destroyAudioReader (ARA::ARAAudioReaderHostRef) noexcept override;

	/* ArchivingController */
	ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef) noexcept override;
	bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef, ARA::ARASize position, ARA::ARASize length, ARA::ARAByte buffer[]) noexcept override;
	bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef, ARA::ARASize position, ARA::ARASize length, const ARA::ARAByte buffer[]) noexcept override;
	void notifyDocumentArchivingProgress (float) noexcept override {}
	void notifyDocumentUnarchivingProgress (float) noexcept override {}
	ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef r) noexcept override { return fromHostRef (r)->id.c_str (); }

	/* ContentAccessController: tempo and bar signatures from the tempo map */
	bool isMusicalContextContentAvailable (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type) noexcept override
	{
		return type == ARA::kARAContentTypeTempoEntries || type == ARA::kARAContentTypeBarSignatures;
	}
	ARA::ARAContentGrade getMusicalContextContentGrade (ARA::ARAMusicalContextHostRef, ARA::ARAContentType) noexcept override { return ARA::kARAContentGradeAdjusted; }
	ARA::ARAContentReaderHostRef createMusicalContextContentReader (ARA::ARAMusicalContextHostRef, ARA::ARAContentType, const ARA::ARAContentTimeRange*) noexcept override;
	bool isAudioSourceContentAvailable (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return false; }
	ARA::ARAContentGrade getAudioSourceContentGrade (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return ARA::kARAContentGradeInitial; }
	ARA::ARAContentReaderHostRef createAudioSourceContentReader (ARA::ARAAudioSourceHostRef, ARA::ARAContentType, const ARA::ARAContentTimeRange*) noexcept override { return nullptr; }
	ARA::ARAInt32 getContentReaderEventCount (ARA::ARAContentReaderHostRef r) noexcept override
	{
		HostContentReader* cr = fromHostRef (r);
		return cr->type == ARA::kARAContentTypeTempoEntries ? (ARA::ARAInt32) cr->tempo.size () : (ARA::ARAInt32) cr->bars.size ();
	}
	const void* getContentReaderDataForEvent (ARA::ARAContentReaderHostRef r, ARA::ARAInt32 i) noexcept override
	{
		HostContentReader* cr = fromHostRef (r);
		return cr->type == ARA::kARAContentTypeTempoEntries ? (void const*) &cr->tempo[i] : (void const*) &cr->bars[i];
	}
	void destroyContentReader (ARA::ARAContentReaderHostRef r) noexcept override { delete fromHostRef (r); }

	/* ModelUpdateController: the plug-in's analysis needs no host action */
	void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef, ARA::ARAAnalysisProgressState, float) noexcept override {}
	void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
	void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
	void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
	void notifyDocumentDataChanged () noexcept override { _session.set_dirty (); }

	/* PlaybackController: the plug-in's transport buttons */
	void requestStartPlayback () noexcept override { _session.request_roll (); }
	void requestStopPlayback () noexcept override { _session.request_stop (); }
	void requestSetPlaybackPosition (ARA::ARATimePosition t) noexcept override { _session.request_locate ((samplepos_t) (t * _session.sample_rate ())); }
	void requestSetCycleRange (ARA::ARATimePosition, ARA::ARATimeDuration) noexcept override {}
	void requestEnableCycle (bool) noexcept override {}
};

std::map<std::pair<Session*, string>, Document*> documents;
std::set<Document*>                              orphans;
std::atomic<int64_t>                             total_read (0);

Document*
find_document (Session& s, ARA::ARAFactory const* f)
{
	auto i = documents.find (std::make_pair (&s, string (f->factoryID)));
	if (i != documents.end ()) {
		return i->second;
	}
	Document* d = new Document (s, f);
	if (!d->dc) {
		delete d;
		return 0;
	}
	documents[std::make_pair (&s, string (f->factoryID))] = d;
	return d;
}

Document::Document (Session& s, ARA::ARAFactory const* f)
	: SessionHandleRef (s)
	, factory (f)
	, host_instance (this, this, this, this, this)
	, dc (0)
	, musical_context (0)
	, editing (false)
	, bound (0)
	, orphaned (false)
{
	static std::set<ARA::ARAFactory const*> initialized;
	if (f->lowestSupportedApiGeneration > ARA::kARAAPIGeneration_2_0_Final || f->highestSupportedApiGeneration < ARA::kARAAPIGeneration_2_0_Final) {
		error << string_compose (_("ARA: %1 needs an ARA version this program does not support."), f->plugInName) << endmsg;
		return;
	}
	if (initialized.insert (f).second) {
		ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAInterfaceConfiguration, assertFunctionAddress)> config;
		config.desiredApiGeneration  = ARA::kARAAPIGeneration_2_0_Final;
		config.assertFunctionAddress = nullptr;
		f->initializeARAWithConfiguration (&config);
	}

	ARA::SizedStruct<ARA_STRUCT_MEMBER (ARADocumentProperties, name)> props;
	string const name = s.name ();
	props.name        = name.c_str ();
	ARA::ARADocumentControllerInstance const* inst = f->createDocumentControllerWithDocument (&host_instance, &props);
	if (!inst) {
		error << string_compose (_("ARA: %1 could not create a document."), f->plugInName) << endmsg;
		return;
	}
	dc = new ARA::Host::DocumentController (inst);

	begin ();
	ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAMusicalContextProperties, color)> mprops;
	mprops.name       = nullptr;
	mprops.orderIndex = 0;
	mprops.color      = nullptr;
	musical_context   = dc->createMusicalContext (toHostRef (this), &mprops);
	end ();

	Temporal::TempoMap::MapChanged.connect_same_thread (connections, std::bind (&Document::tempo_map_changed, this));
	restore ();
}

Document::~Document ()
{
	connections.drop_connections ();
	if (!dc) {
		return;
	}
	begin ();
	for (auto& [r, pr] : regions) {
		dc->destroyPlaybackRegion (pr->ref);
		delete pr;
	}
	for (auto& [k, m] : modifications) {
		dc->destroyAudioModification (m->ref);
		delete m;
	}
	for (auto& [k, a] : sources) {
		dc->enableAudioSourceSamplesAccess (a->ref, false);
		dc->destroyAudioSource (a->ref);
		delete a;
	}
	for (auto& [k, q] : sequences) {
		dc->destroyRegionSequence (q->ref);
		delete q;
	}
	if (musical_context) {
		dc->destroyMusicalContext (musical_context);
	}
	end ();
	dc->destroyDocumentController ();
	delete dc;
}

void drop_region (Document*, HostPlaybackRegion*, bool suspend = true);

void
Document::session_going_away ()
{
	for (auto i = documents.begin (); i != documents.end ();) {
		if (i->second == this) {
			i = documents.erase (i);
		} else {
			++i;
		}
	}
	/* the renderers let go of the regions now; the document itself must
	 * outlive the plug-in instances bound to it (ARA 2) */
	while (!regions.empty ()) {
		HostPlaybackRegion* pr = regions.begin ()->second;
		regions.erase (regions.begin ());
		drop_region (this, pr);
	}
	connections.drop_connections ();
	/* the session's sources go with it */
	begin ();
	for (auto& [k, a] : sources) {
		dc->enableAudioSourceSamplesAccess (a->ref, false);
	}
	end ();
	for (auto& [k, a] : sources) {
		a->channels.clear ();
	}
	SessionHandleRef::session_going_away ();
	if (bound > 0) {
		orphaned = true;
		orphans.insert (this);
	} else {
		delete this;
	}
}

/* key: the region's channel sources */
HostAudioSource*
Document::audio_source (AudioRegion const& r)
{
	string key;
	for (uint32_t c = 0; c < r.n_channels (); ++c) {
		key += (c ? "+" : "") + r.audio_source (c)->id ().to_s ();
	}
	auto i = sources.find (key);
	if (i != sources.end ()) {
		++i->second->users;
		return i->second;
	}
	HostAudioSource* a = new HostAudioSource;
	a->doc   = this;
	a->key   = key;
	a->name  = r.audio_source (0)->name ();
	a->users = 1;
	a->length = r.audio_source (0)->length ().samples ();
	for (uint32_t c = 0; c < r.n_channels (); ++c) {
		a->channels.push_back (r.audio_source (c));
	}

	ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAAudioSourceProperties, channelArrangement)> props;
	props.name                       = a->name.c_str ();
	props.persistentID               = a->key.c_str ();
	props.sampleCount                = a->length;
	props.sampleRate                 = _session.sample_rate ();
	props.channelCount               = (ARA::ARAChannelCount) a->channels.size ();
	props.merits64BitSamples         = ARA::kARAFalse;
	props.channelArrangementDataType = ARA::kARAChannelArrangementUndefined;
	props.channelArrangement         = nullptr;
	a->ref                           = dc->createAudioSource (toHostRef (a), &props);
	sources[key]                     = a;
	return a;
}

/* one modification per original region; the pieces of a split share it */
HostAudioModification*
Document::modification (AudioRegion const& r, HostAudioSource* src)
{
	string id;
	if (XMLNode* x = const_cast<AudioRegion&> (r).extra_xml (X_("ARA"))) {
		x->get_property (X_("modification"), id);
	}
	if (id.empty ()) {
		id = PBD::ID ().to_s ();
		XMLNode* x = const_cast<AudioRegion&> (r).extra_xml (X_("ARA"), true);
		x->set_property (X_("modification"), id);
	}
	auto i = modifications.find (id);
	if (i != modifications.end ()) {
		++i->second->users;
		return i->second;
	}
	HostAudioModification* m = new HostAudioModification;
	m->doc    = this;
	m->id     = id;
	m->source = src;
	m->users  = 1;
	ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAAudioModificationProperties, persistentID)> props;
	props.name         = r.name ().c_str ();
	props.persistentID = m->id.c_str ();
	m->ref             = dc->createAudioModification (src->ref, toHostRef (m), &props);
	modifications[id]  = m;
	return m;
}

HostRegionSequence*
Document::sequence (AudioRegion const& r)
{
	/* the track that the playlist was made for (while a session loads,
	 * playlists come before their tracks) */
	std::shared_ptr<Playlist> pl    = r.playlist ();
	PBD::ID const             key   = pl ? pl->get_orig_track_id () : PBD::ID (0);
	std::shared_ptr<Route>    route = _session.route_by_id (key);
	auto          i   = sequences.find (key);
	if (i != sequences.end ()) {
		++i->second->users;
		return i->second;
	}
	HostRegionSequence* q = new HostRegionSequence;
	q->doc   = this;
	q->route = key;
	q->users = 1;
	q->name  = route ? route->name () : pl ? pl->name () : string (_("Track"));
	ARA::SizedStruct<ARA_STRUCT_MEMBER (ARARegionSequenceProperties, color)> props;
	props.name              = q->name.c_str ();
	props.orderIndex        = route ? (ARA::ARAInt32) route->presentation_info ().order () : 0;
	props.musicalContextRef = musical_context;
	props.color             = nullptr;
	q->ref                  = dc->createRegionSequence (toHostRef (q), &props);
	sequences[key]          = q;
	return q;
}

void
Document::fill_region_properties (HostPlaybackRegion const& pr, ARA::ARAPlaybackRegionProperties& p) const
{
	double const sr                  = _session.sample_rate ();
	p.transformationFlags            = ARA::kARAPlaybackTransformationNoChanges;
	p.startInModificationTime        = pr.region->start ().samples () / sr;
	p.durationInModificationTime     = pr.region->length ().samples () / sr;
	p.startInPlaybackTime            = pr.region->position ().samples () / sr;
	p.durationInPlaybackTime         = p.durationInModificationTime;
	p.musicalContextRef              = nullptr;
	p.regionSequenceRef              = pr.sequence->ref;
	p.name                           = pr.name.c_str ();
	p.color                          = nullptr;
}

/* moves and trims of the region: the plug-in's edits follow */
void
Document::region_changed (HostPlaybackRegion* pr)
{
	ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAPlaybackRegionProperties, color)> props;
	pr->name = pr->region->name ();
	fill_region_properties (*pr, props);
	begin ();
	dc->updatePlaybackRegionProperties (pr->ref, &props);
	end ();
}

void
Document::tempo_map_changed ()
{
	if (!musical_context) {
		return;
	}
	begin ();
	dc->updateMusicalContextContent (musical_context, nullptr, ARA::ContentUpdateScopes::everythingIsAffected ());
	end ();
}

ARA::ARAContentReaderHostRef
Document::createMusicalContextContentReader (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type, const ARA::ARAContentTimeRange*) noexcept
{
	HostContentReader* cr = new HostContentReader;
	cr->type              = type;

	Temporal::TempoMap::SharedPtr tmap (Temporal::TempoMap::use ());
	double const                  sr = _session.sample_rate ();
	if (type == ARA::kARAContentTypeTempoEntries) {
		/* a tempo entry at every tempo point, plus one a beat later, so
		 * that each constant-tempo stretch is described (ARA needs >= 2) */
		for (auto const& t : tmap->tempos ()) {
			Temporal::timepos_t const at (t.beats ());
			cr->tempo.push_back ({ at.samples () / sr, Temporal::DoubleableBeats (t.beats ()).to_double () });
		}
		Temporal::Beats const last = cr->tempo.empty () ? Temporal::Beats () : Temporal::Beats::from_double (cr->tempo.back ().quarterPosition);
		Temporal::timepos_t const next (last + Temporal::Beats (1, 0));
		cr->tempo.push_back ({ next.samples () / sr, Temporal::DoubleableBeats (last + Temporal::Beats (1, 0)).to_double () });
	} else {
		for (auto const& m : tmap->meters ()) {
			cr->bars.push_back ({ (ARA::ARAInt32) m.divisions_per_bar (), (ARA::ARAInt32) m.note_value (), Temporal::DoubleableBeats (m.beats ()).to_double () });
		}
	}
	return toHostRef (cr);
}

ARA::ARAAudioReaderHostRef
Document::createAudioReaderForSource (ARA::ARAAudioSourceHostRef s, bool use64) noexcept
{
	HostAudioReader* r = new HostAudioReader;
	r->source          = fromHostRef (s);
	r->use64           = use64;
	return toHostRef (r);
}

bool
Document::readAudioSamples (ARA::ARAAudioReaderHostRef rr, ARA::ARASamplePosition pos, ARA::ARASampleCount count, void* const buffers[]) noexcept
{
	HostAudioReader* r = fromHostRef (rr);
	for (size_t c = 0; c < r->source->channels.size (); ++c) {
		float* dst;
		if (r->use64) {
			r->tmp.resize (count);
			dst = r->tmp.data ();
		} else {
			dst = static_cast<float*> (buffers[c]);
		}
		/* outside the source: silence */
		memset (dst, 0, sizeof (float) * count);
		samplepos_t const from = std::max<samplepos_t> (0, pos);
		samplepos_t const to   = std::min<samplepos_t> (r->source->length, pos + count);
		if (to > from) {
			r->source->channels[c]->read (dst + (from - pos), from, to - from);
			total_read += to - from;
		}
		if (r->use64) {
			double* d = static_cast<double*> (buffers[c]);
			for (ARA::ARASampleCount i = 0; i < count; ++i) {
				d[i] = dst[i];
			}
		}
	}
	return true;
}

void
Document::destroyAudioReader (ARA::ARAAudioReaderHostRef r) noexcept
{
	delete fromHostRef (r);
}

ARA::ARASize
Document::getArchiveSize (ARA::ARAArchiveReaderHostRef r) noexcept
{
	return fromHostRef (r)->bytes.size ();
}

bool
Document::readBytesFromArchive (ARA::ARAArchiveReaderHostRef r, ARA::ARASize position, ARA::ARASize length, ARA::ARAByte buffer[]) noexcept
{
	ArchiveData* a = fromHostRef (r);
	if (position + length > a->bytes.size ()) {
		return false;
	}
	memcpy (buffer, a->bytes.data () + position, length);
	return true;
}

bool
Document::writeBytesToArchive (ARA::ARAArchiveWriterHostRef w, ARA::ARASize position, ARA::ARASize length, const ARA::ARAByte buffer[]) noexcept
{
	ArchiveData* a = fromHostRef (w);
	if (a->bytes.size () < position + length) {
		a->bytes.resize (position + length);
	}
	memcpy (a->bytes.data () + position, buffer, length);
	return true;
}

/* <session>/plugins/ara-<factory>-<snapshot>.dat */
string
Document::archive_path (string const& snapshot) const
{
	string id = factory->factoryID;
	for (auto& c : id) {
		if (!isalnum ((unsigned char) c)) {
			c = '_';
		}
	}
	return Glib::build_filename (_session.plugins_dir (), string_compose ("ara-%1-%2.dat", id, snapshot));
}

void
Document::save (string const& snapshot)
{
	ArchiveData a;
	a.id = factory->documentArchiveID;
	if (!dc->storeObjectsToArchive (toHostRef (&a), nullptr)) {
		error << string_compose (_("ARA: %1 could not save its data."), factory->plugInName) << endmsg;
		return;
	}
	string const path = archive_path (snapshot);
	GError*      err  = 0;
	if (!g_file_set_contents (path.c_str (), (gchar const*) a.bytes.data (), a.bytes.size (), &err)) {
		error << string_compose (_("ARA: could not write %1"), path) << endmsg;
		if (err) {
			g_error_free (err);
		}
	}
}

/* the plug-in restores what it saved for the objects as they are created;
 * ARA 2 restores objects that exist, so this runs again after each new
 * audio source/modification was added (see add_region) */
void
Document::restore ()
{
}

/* the data saved for the current snapshot */
bool
load_archive (Document* d, ArchiveData& a)
{
	string const path = d->archive_path (d->sess ().snap_name ());
	gchar*       buf  = 0;
	gsize        len  = 0;
	if (!g_file_get_contents (path.c_str (), &buf, &len, 0)) {
		return false;
	}
	a.bytes.assign ((uint8_t*) buf, (uint8_t*) buf + len);
	a.id = d->factory->documentArchiveID;
	g_free (buf);
	return true;
}

/* renderers get or lose playback regions only while the plug-in is inactive
 * (VST3 binding of ARA 2) */
struct Suspended {
	Suspended (std::shared_ptr<Plugin> p) : plugin (p), was_active (p && p->ara_suspend ()) {}
	~Suspended () { if (plugin) { plugin->ara_resume (was_active); } }
	std::shared_ptr<Plugin> plugin;
	bool                    was_active;
};

/* suspend: false if the plug-in is inactive already (going away) */
void
drop_region (Document* d, HostPlaybackRegion* pr, bool suspend)
{
	ARA::ARAPlugInExtensionInstance const* inst = static_cast<ARA::ARAPlugInExtensionInstance const*> (pr->extension);
	{
		Suspended s (suspend ? pr->plugin.lock () : std::shared_ptr<Plugin> ());
		if (inst && inst->playbackRendererInterface) {
			ARA::Host::PlaybackRenderer (inst).removePlaybackRegion (pr->ref);
		}
		if (inst && inst->editorRendererInterface) {
			ARA::Host::EditorRenderer (inst).removePlaybackRegion (pr->ref);
		}
	}
	d->begin ();
	d->dc->destroyPlaybackRegion (pr->ref);
	if (--pr->modification->users == 0) {
		d->dc->destroyAudioModification (pr->modification->ref);
		d->modifications.erase (pr->modification->id);
		HostAudioSource* src = pr->modification->source;
		delete pr->modification;
		(void) src;
	}
	if (--pr->sequence->users == 0) {
		d->dc->destroyRegionSequence (pr->sequence->ref);
		d->sequences.erase (pr->sequence->route);
		delete pr->sequence;
	}
	d->end ();
	/* audio sources stay until the document goes: the plug-in's analysis
	 * of a file is kept, and undo may bring the region back */
	delete pr;
}

Document*
find_by_controller (void* dc)
{
	for (auto& [k, d] : documents) {
		if (d->dc->getRef () == dc) {
			return d;
		}
	}
	for (auto d : orphans) {
		if (d->dc->getRef () == dc) {
			return d;
		}
	}
	return 0;
}

} /* anonymous namespace */

void
ARAHost::plugin_bound (void* dc)
{
	if (Document* d = find_by_controller (dc)) {
		++d->bound;
	}
}

void
ARAHost::plugin_released (void* dc)
{
	Document* d = find_by_controller (dc);
	if (d && --d->bound == 0 && d->orphaned) {
		orphans.erase (d);
		delete d;
	}
}

void*
ARAHost::document_controller (Session& s, void const* f)
{
	Document* d = find_document (s, static_cast<ARA::ARAFactory const*> (f));
	return d ? d->dc->getRef () : nullptr;
}

void
ARAHost::add_region (std::shared_ptr<AudioRegion> region, std::shared_ptr<Plugin> plugin)
{
	void const* const ext = plugin->ara_extension ();
	Document*         d   = find_document (region->session (), static_cast<ARA::ARAFactory const*> (plugin->ara_factory ()));
	if (!d) {
		return;
	}
	auto old = d->regions.find (region.get ());
	if (old != d->regions.end ()) {
		std::shared_ptr<Playlist> pl = region->playlist ();
		if (old->second->extension == ext && pl && old->second->sequence->route == pl->get_orig_track_id ()) {
			return; /* nothing changed */
		}
		/* another plug-in instance, or moved to another track */
		HostPlaybackRegion* pr = old->second;
		d->regions.erase (old);
		drop_region (d, pr);
	}

	d->begin ();
	HostPlaybackRegion* pr = new HostPlaybackRegion;
	pr->doc                = d;
	pr->region             = region.get ();
	pr->extension          = ext;
	pr->plugin             = plugin;
	pr->name               = region->name ();
	HostAudioSource* src   = d->audio_source (*region);
	bool const       fresh = src->users == 1;
	size_t const     nmods = d->modifications.size ();
	pr->modification       = d->modification (*region, src);
	pr->sequence           = d->sequence (*region);

	ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAPlaybackRegionProperties, color)> props;
	d->fill_region_properties (*pr, props);
	pr->ref = d->dc->createPlaybackRegion (pr->modification->ref, toHostRef (pr), &props);

	/* the plug-in's data saved with the session for this source and modification */
	if (fresh || d->modifications.size () != nmods) {
		ArchiveData a;
		if (load_archive (d, a)) {
			ARA::SizedStruct<ARA_STRUCT_MEMBER (ARARestoreObjectsFilter, audioModificationCurrentIDs)> filter;
			ARA::ARAPersistentID       sid = src->key.c_str ();
			ARA::ARAPersistentID       mid = pr->modification->id.c_str ();
			filter.documentData             = ARA::kARAFalse;
			filter.audioSourceIDsCount      = fresh ? 1 : 0;
			filter.audioSourceArchiveIDs    = &sid;
			filter.audioSourceCurrentIDs    = &sid;
			filter.audioModificationIDsCount = 1;
			filter.audioModificationArchiveIDs = &mid;
			filter.audioModificationCurrentIDs = &mid;
			d->dc->restoreObjectsFromArchive (toHostRef (&a), &filter);
		}
	}
	d->end ();

	if (fresh) {
		d->dc->enableAudioSourceSamplesAccess (src->ref, true);
	}

	ARA::ARAPlugInExtensionInstance const* inst = static_cast<ARA::ARAPlugInExtensionInstance const*> (ext);
	{
		Suspended s (plugin);
		if (inst->playbackRendererInterface) {
			ARA::Host::PlaybackRenderer (inst).addPlaybackRegion (pr->ref);
		}
		if (inst->editorRendererInterface) {
			ARA::Host::EditorRenderer (inst).addPlaybackRegion (pr->ref);
		}
	}
	if (inst->editorViewInterface) {
		/* show this region when the plug-in's window opens */
		ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAViewSelection, timeRange)> sel;
		sel.playbackRegionRefsCount = 1;
		sel.playbackRegionRefs      = &pr->ref;
		sel.regionSequenceRefsCount = 0;
		sel.regionSequenceRefs      = nullptr;
		sel.timeRange               = nullptr;
		ARA::Host::EditorView (inst).notifySelection (&sel);
	}

	region->PropertyChanged.connect_same_thread (pr->changed, [pr] (PBD::PropertyChange const& what) {
		if (what.contains (Properties::start) || what.contains (Properties::length) || what.contains (Properties::name)) {
			pr->doc->region_changed (pr);
		}
	});
	d->regions[region.get ()] = pr;
}

void
ARAHost::remove_region (AudioRegion const* region, void const* ext)
{
	for (auto& [k, d] : documents) {
		auto i = d->regions.find (region);
		if (i != d->regions.end () && (!ext || i->second->extension == ext)) {
			HostPlaybackRegion* pr = i->second;
			d->regions.erase (i);
			drop_region (d, pr);
			return;
		}
	}
}

void
ARAHost::region_destroyed (AudioRegion const* region)
{
	remove_region (region, nullptr);
}

void
ARAHost::plugin_destroyed (void const* ext)
{
	for (auto& [k, d] : documents) {
		for (auto i = d->regions.begin (); i != d->regions.end ();) {
			if (i->second->extension == ext) {
				HostPlaybackRegion* pr = i->second;
				i                      = d->regions.erase (i);
				drop_region (d, pr, false);
			} else {
				++i;
			}
		}
	}
}

void
ARAHost::save (Session& s, string const& snapshot)
{
	for (auto& [k, d] : documents) {
		if (k.first == &s) {
			d->save (snapshot);
		}
	}
}

bool
ARAHost::region_notes (AudioRegion const* region, vector<Note>& notes)
{
	for (auto& [k, d] : documents) {
		auto i = d->regions.find (region);
		if (i == d->regions.end ()) {
			continue;
		}
		ARA::ARAPlaybackRegionRef const ref = i->second->ref;
		if (!d->dc->isPlaybackRegionContentAvailable (ref, ARA::kARAContentTypeNotes)) {
			return false;
		}
		ARA::ARAContentReaderRef cr = d->dc->createPlaybackRegionContentReader (ref, ARA::kARAContentTypeNotes, nullptr);
		if (!cr) {
			return false;
		}
		ARA::ARAInt32 const n = d->dc->getContentReaderEventCount (cr);
		for (ARA::ARAInt32 e = 0; e < n; ++e) {
			ARA::ARAContentNote const* nt = static_cast<ARA::ARAContentNote const*> (d->dc->getContentReaderDataForEvent (cr, e));
			notes.push_back ({ nt->frequency, nt->startPosition, nt->noteDuration });
		}
		d->dc->destroyContentReader (cr);
		return true;
	}
	return false;
}

void
ARAHost::idle ()
{
	for (auto& [k, d] : documents) {
		d->dc->notifyModelUpdates ();
	}
}

size_t
ARAHost::playback_regions (Session& s)
{
	size_t n = 0;
	for (auto& [k, d] : documents) {
		if (k.first == &s) {
			n += d->regions.size ();
		}
	}
	return n;
}

int64_t
ARAHost::samples_read ()
{
	return total_read;
}

#endif /* HAVE_ARA */
