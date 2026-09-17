import { HttpClient } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import { EvidenceRecord, IncidentRecord } from '../models';
import { ApiConfigService } from './api-config.service';

@Injectable({ providedIn: 'root' })
export class IncidentService {
  private readonly http = inject(HttpClient);
  private readonly config = inject(ApiConfigService);
  private get base(): string {
    return this.config.baseUrl();
  }

  list(deviceId?: string, limit = 200): Observable<IncidentRecord[]> {
    const params: Record<string, string | number> = { limit };
    if (deviceId) params['deviceId'] = deviceId;
    return this.http.get<IncidentRecord[]>(`${this.base}/incidents`, { params });
  }

  get(incidentId: string): Observable<IncidentRecord> {
    return this.http.get<IncidentRecord>(`${this.base}/incidents/${encodeURIComponent(incidentId)}`);
  }

  evidence(incidentId: string): Observable<EvidenceRecord[]> {
    return this.http.get<EvidenceRecord[]>(`${this.base}/incidents/${encodeURIComponent(incidentId)}/evidence`);
  }

  evidenceFileUrl(evidenceId: number): string {
    return `${this.base}/evidence/${evidenceId}/file`;
  }
}
