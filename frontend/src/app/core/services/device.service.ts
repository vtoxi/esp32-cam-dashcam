import { HttpClient } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import { Device, TelemetryRecord } from '../models';
import { ApiConfigService } from './api-config.service';

@Injectable({ providedIn: 'root' })
export class DeviceService {
  private readonly http = inject(HttpClient);
  private readonly config = inject(ApiConfigService);
  private get base(): string {
    return this.config.baseUrl();
  }

  list(): Observable<Device[]> {
    return this.http.get<Device[]>(`${this.base}/devices`);
  }

  get(id: string): Observable<Device> {
    return this.http.get<Device>(`${this.base}/devices/${encodeURIComponent(id)}`);
  }

  telemetry(id: string, limit = 100): Observable<TelemetryRecord[]> {
    return this.http.get<TelemetryRecord[]>(`${this.base}/devices/${encodeURIComponent(id)}/telemetry`, {
      params: { limit },
    });
  }
}
