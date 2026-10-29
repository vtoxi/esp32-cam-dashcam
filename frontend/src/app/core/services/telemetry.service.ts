import { HttpClient } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import { EventRecord } from '../models';
import { ApiConfigService } from './api-config.service';

@Injectable({ providedIn: 'root' })
export class EventLogService {
  private readonly http = inject(HttpClient);
  private readonly config = inject(ApiConfigService);
  private get base(): string {
    return this.config.baseUrl();
  }

  list(deviceId?: string, limit = 200): Observable<EventRecord[]> {
    const params: Record<string, string | number> = { limit };
    if (deviceId) params['deviceId'] = deviceId;
    return this.http.get<EventRecord[]>(`${this.base}/events`, { params });
  }
}
