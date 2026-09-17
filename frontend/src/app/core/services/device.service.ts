import { HttpClient, HttpContext } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import { Device, TelemetryRecord } from '../models';
import { ADMIN_GATED } from '../interceptors/admin-key.interceptor';
import { ApiConfigService } from './api-config.service';

export interface PreProvisionDeviceRequest {
  hardwareProfile?: string;
  nodeId?: string;
  tenantId?: string;
}

export interface PreProvisionDeviceResponse {
  deviceId: string;
  credential: string;
  hardwareProfile: string;
  nodeId: string;
  tenantId: string | null;
}

@Injectable({ providedIn: 'root' })
export class DeviceService {
  private readonly http = inject(HttpClient);
  private readonly config = inject(ApiConfigService);
  private get base(): string {
    return this.config.baseUrl();
  }
  private get adminContext(): HttpContext {
    return new HttpContext().set(ADMIN_GATED, true);
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

  /** Admin-key gated (backend/.../DeviceEndpoints.cs). Pre-provisions a device
   * record + credential for a gateway that hasn't self-registered yet — the
   * returned credential must be entered into that gateway's own Settings page
   * (Remote Backend > Device ID / Credential) or BACKENDCONFIG serial command. */
  preProvision(request: PreProvisionDeviceRequest): Observable<PreProvisionDeviceResponse> {
    return this.http.post<PreProvisionDeviceResponse>(`${this.base}/devices`, request, {
      context: this.adminContext,
    });
  }

  /** Admin-key gated. Only tenantId is editable — hardwareProfile/firmwareVersion/nodeId
   * are firmware-reported and get overwritten on the device's next register/heartbeat. */
  updateTenant(id: string, tenantId: string | null): Observable<{ id: string; tenantId: string | null }> {
    return this.http.patch<{ id: string; tenantId: string | null }>(
      `${this.base}/devices/${encodeURIComponent(id)}`,
      { tenantId },
      { context: this.adminContext },
    );
  }

  /** Admin-key gated. Removes the device record only — historical telemetry/events/
   * incidents/evidence under this deviceId are kept (documented backend behavior). */
  delete(id: string): Observable<{ ok: boolean }> {
    return this.http.delete<{ ok: boolean }>(`${this.base}/devices/${encodeURIComponent(id)}`, {
      context: this.adminContext,
    });
  }
}
