import { DestroyRef, Injectable, effect, inject, signal } from '@angular/core';
import { Subject } from 'rxjs';
import { ActivityItem, StreamEnvelope } from '../models';
import { ApiConfigService } from './api-config.service';

const MAX_ACTIVITY_ITEMS = 300;

/**
 * Wraps the backend's Server-Sent Events endpoint (Phase 21.6,
 * GET /api/v1/stream) in a signal-based, auto-reconnecting client. Native
 * EventSource (not a library) — the endpoint is plain `text/event-stream`,
 * no custom event names, one JSON envelope per message
 * (`{schemaVersion, type, deviceId, timestamp, payload}`).
 */
@Injectable({ providedIn: 'root' })
export class EventStreamService {
  private readonly config = inject(ApiConfigService);
  private readonly destroyRef = inject(DestroyRef);

  private source: EventSource | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  readonly connected = signal(false);
  readonly activity = signal<ActivityItem[]>([]);

  /** For components that want to react to specific event types (e.g. toast on incident.created). */
  readonly events$ = new Subject<ActivityItem>();

  constructor() {
    effect(() => {
      const url = this.config.baseUrl();
      this.reconnect(url);
    });
    this.destroyRef.onDestroy(() => this.teardown());
  }

  clear(): void {
    this.activity.set([]);
  }

  private reconnect(baseUrl: string): void {
    this.teardown();
    try {
      this.source = new EventSource(`${baseUrl}/stream`);
    } catch {
      this.connected.set(false);
      return;
    }
    this.source.onopen = () => this.connected.set(true);
    this.source.onerror = () => {
      this.connected.set(false);
      // EventSource retries on its own by default; nothing extra needed here.
    };
    this.source.onmessage = (event) => this.handleMessage(event.data);
  }

  private handleMessage(raw: string): void {
    let envelope: StreamEnvelope;
    try {
      envelope = JSON.parse(raw);
    } catch {
      return;
    }
    const item: ActivityItem = {
      id: `${envelope.timestamp}-${Math.random().toString(36).slice(2, 8)}`,
      type: envelope.type,
      deviceId: envelope.deviceId,
      timestamp: envelope.timestamp,
      payload: envelope.payload,
    };
    this.activity.update((items) => [item, ...items].slice(0, MAX_ACTIVITY_ITEMS));
    this.events$.next(item);
  }

  private teardown(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.source?.close();
    this.source = null;
    this.connected.set(false);
  }
}
