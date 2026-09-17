import { Injectable, computed, signal } from '@angular/core';

const STORAGE_KEY = 'carsentinel.adminKey';

/**
 * The admin key gates command-issuance and webhook-management endpoints
 * (backend/src/CarSentinel.Backend/Endpoints/CommandEndpoints.cs,
 * WebhookEndpoints.cs — X-Admin-Key header vs. the backend's Admin:ApiKey
 * config). This is a reference dashboard with no user-account model of its
 * own (documented gap, docs/BACKEND.md's multi-tenant-readiness note), so the
 * key is held client-side only, in sessionStorage (cleared when the tab
 * closes, never persisted longer than that) — never in a cookie, never sent
 * anywhere but the Authorization-adjacent header on admin requests.
 */
@Injectable({ providedIn: 'root' })
export class AdminAuthService {
  private readonly key = signal(sessionStorage.getItem(STORAGE_KEY) ?? '');
  readonly hasKey = computed(() => this.key().length > 0);
  readonly currentKey = computed(() => this.key());

  setKey(value: string): void {
    const trimmed = value.trim();
    this.key.set(trimmed);
    if (trimmed) {
      sessionStorage.setItem(STORAGE_KEY, trimmed);
    } else {
      sessionStorage.removeItem(STORAGE_KEY);
    }
  }

  clear(): void {
    this.setKey('');
  }
}
