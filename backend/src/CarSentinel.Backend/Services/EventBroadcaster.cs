using System.Collections.Concurrent;
using System.Threading.Channels;

namespace CarSentinel.Backend.Services;

// Phase 21.6 — Real-Time API. Server-Sent Events, not WebSocket: one-directional
// (server -> browser) is all docs/REMOTE_ACCESS.md Section 5's flow needs
// ("Gateway -> Backend -> WebSocket/SSE -> Browser"), and SSE needs nothing beyond a
// streamed HTTP response (no extra package, no upgrade handshake, works through the
// same reverse proxies plain HTTP already does). Revisit as WebSocket only if a
// future client genuinely needs to push data back over the same connection.
//
// In-memory only, single-process — every connected browser gets a Channel it reads
// from; Publish() fans out to all of them. Fine for a reference backend; a real
// multi-instance deployment would need a shared pub/sub (Redis, etc.) instead, a
// scaling concern out of scope for this reference implementation (docs/BACKEND.md's
// own "avoid unnecessary infrastructure" reasoning — don't add Redis until there's a
// second instance to justify it).
public class EventBroadcaster
{
    private readonly ConcurrentDictionary<Guid, Channel<string>> subscribers = new();

    public Guid Subscribe(out ChannelReader<string> reader)
    {
        var channel = Channel.CreateBounded<string>(new BoundedChannelOptions(64)
        {
            FullMode = BoundedChannelFullMode.DropOldest,
        });
        var id = Guid.NewGuid();
        subscribers[id] = channel;
        reader = channel.Reader;
        return id;
    }

    public void Unsubscribe(Guid id)
    {
        if (subscribers.TryRemove(id, out var channel))
        {
            channel.Writer.TryComplete();
        }
    }

    public void Publish(string eventType, string deviceId, object payload)
    {
        var envelope = System.Text.Json.JsonSerializer.Serialize(new
        {
            schemaVersion = 1,
            type = eventType,
            deviceId,
            timestamp = DateTime.UtcNow,
            payload,
        });
        foreach (var kvp in subscribers)
        {
            kvp.Value.Writer.TryWrite(envelope);
        }
    }

    public int SubscriberCount => subscribers.Count;
}
