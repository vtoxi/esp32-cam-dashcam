using CarSentinel.Backend.Services;

namespace CarSentinel.Backend.Endpoints;

public static class StreamEndpoints
{
    public static void MapStreamEndpoints(this IEndpointRouteBuilder app)
    {
        // GET /api/v1/stream — Server-Sent Events. Anonymous, same posture as the
        // other GET endpoints (docs/BACKEND.md's documented gap: no user-account
        // model yet). A browser: new EventSource('/api/v1/stream').
        app.MapGet("/api/v1/stream", async (HttpContext ctx, EventBroadcaster broadcaster, CancellationToken requestAborted) =>
        {
            ctx.Response.Headers.ContentType = "text/event-stream";
            ctx.Response.Headers.CacheControl = "no-cache";
            ctx.Response.Headers["X-Accel-Buffering"] = "no";  // don't let a reverse proxy buffer this away

            var subId = broadcaster.Subscribe(out var reader);
            try
            {
                // A comment line first so the browser's EventSource fires 'open'
                // immediately rather than waiting for the first real event.
                await ctx.Response.WriteAsync(": connected\n\n", requestAborted);
                await ctx.Response.Body.FlushAsync(requestAborted);

                await foreach (var message in reader.ReadAllAsync(requestAborted))
                {
                    await ctx.Response.WriteAsync($"data: {message}\n\n", requestAborted);
                    await ctx.Response.Body.FlushAsync(requestAborted);
                }
            }
            catch (OperationCanceledException)
            {
                // Client disconnected — expected, not an error.
            }
            finally
            {
                broadcaster.Unsubscribe(subId);
            }
        }).WithTags("Real-Time").ExcludeFromDescription();
        // Excluded from the OpenAPI doc: Swashbuckle models request/response bodies,
        // not an indefinitely-streamed text/event-stream — documented separately in
        // docs/REMOTE_ACCESS.md instead of trying to force it into the OpenAPI schema.
    }
}
