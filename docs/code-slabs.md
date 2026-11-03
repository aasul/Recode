# Immutable executable page snapshots

Managed integer translations can share one page. The first translation and
large entries use isolated mappings. When an additional entry fits with an
existing unpinned page's live entries and a spare page fits the executable budget,
the allocator creates a new RW page, copies the existing code, emits the new
translation, seals the entire page RX, then switches all participating entries
under the runtime mutex. The old mapping is released afterward. No active
mapping is modified. There are no writable aliases, RWX mappings or page write
permission toggles. Accumulator and trace translations remain isolated.

This deliberately uses page replacement rather than appending to RX memory.
Publication is synchronous. The implementation scans runtime translations and
copies at most one page per successful packing. It trades additional compilation
latency and a transient spare page for lower committed storage. A full budget,
pinned source page, oversized translation or lack of a suitable page uses the
existing isolated path. One-page budgets retain their previous behavior.

Entries start at 16-byte boundaries. Both scalar and batch bodies contain only
internal relative branches and no embedded addresses, external calls or
RIP-relative data. Copying them together preserves every displacement. Emission
into a page offset uses the same local offsets as isolated generation. Direct
scalar/batch sentinel tests and repeated page replacements verify relocated code.
No claim is made that arbitrary native bytes are relocatable.

A slab owns its mapping and counts member translations, not handles. Aliases
retain the existing translation identity and pin. Removing one member only
drops its ownership; other members remain callable. The final member releases
the page. A failed final release retains that disabled member and ownership for
retry. Eviction may remove several entries before a shared page can be freed.
An active pin protects its translation and prevents copying/reclaiming its page.
Other unpinned members may still be detached. No address is reused while an old
caller can execute it. All selection, ownership changes and publication use the
same mutex as existing cache acquisition; native calls execute outside it.

If sealing fails, the unpublished page is released. Failed rollback remains
owned in the runtime's retired slab list. If releasing a replaced page fails,
the new entries stay valid and the old immutable mapping enters that same list.
Both cases remain charged to the executable budget and last_cache_error;
clear, capacity acquisition and destruction retry retired mappings. Cleanup
failure can therefore prevent compilation or destruction without losing ownership.

executable_bytes and peak_executable_bytes count all live page-rounded mappings,
including RW rollback failures and retired snapshots. Snapshot creation charges
the new page before subtracting a successfully unmapped old page. generated_bytes
counts currently callable whole-function and trace text. occupied_bytes counts
aligned live slab entries plus callable isolated whole-function and trace text. slab_free_bytes includes
holes and unused tails of live immutable slabs, reclaimable only by replacement.
executable_mappings includes isolated/trace/retired mappings;
retired_executable_bytes counts mappings with no callable entry. These are
requested mapping/text ledgers, not total allocator memory or process RSS.
Statistics queries scan the mapping owners and translations under the mutex.
