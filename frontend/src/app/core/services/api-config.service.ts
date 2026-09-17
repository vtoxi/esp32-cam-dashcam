import { Injectable, signal } from '@angular/core';
import { environment } from '../../../environments/environment';

const STORAGE_KEY = 'carsentinel.apiBaseUrl';

/**
 * The compiled-in environment.apiBaseUrl is the default, but this is a
 * reference dashboard someone may point at a different backend instance
 * without a rebuild — Settings lets a viewer override it, persisted per
 * browser (localStorage), never sent anywhere.
 */
@Injectable({ providedIn: 'root' })
export class ApiConfigService {
  private readonly stored = localStorage.getItem(STORAGE_KEY);
  readonly baseUrl = signal(this.stored && this.stored.trim() ? this.stored : environment.apiBaseUrl);

  setBaseUrl(url: string): void {
    const trimmed = url.trim().replace(/\/+$/, '');
    if (!trimmed) return;
    localStorage.setItem(STORAGE_KEY, trimmed);
    this.baseUrl.set(trimmed);
  }

  resetToDefault(): void {
    localStorage.removeItem(STORAGE_KEY);
    this.baseUrl.set(environment.apiBaseUrl);
  }

  get defaultUrl(): string {
    return environment.apiBaseUrl;
  }
}
