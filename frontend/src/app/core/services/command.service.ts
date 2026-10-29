import { HttpClient, HttpContext } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import { CommandRecord, IssueCommandRequest, IssueCommandResponse } from '../models';
import { ADMIN_GATED } from '../interceptors/admin-key.interceptor';
import { ApiConfigService } from './api-config.service';

@Injectable({ providedIn: 'root' })
export class CommandService {
  private readonly http = inject(HttpClient);
  private readonly config = inject(ApiConfigService);
  private get base(): string {
    return this.config.baseUrl();
  }

  history(deviceId: string, limit = 50): Observable<CommandRecord[]> {
    return this.http.get<CommandRecord[]>(`${this.base}/devices/${encodeURIComponent(deviceId)}/commands`, {
      params: { limit },
    });
  }

  /** Admin-key gated — backend/.../CommandEndpoints.cs refuses with 503 if no Admin:ApiKey is configured. */
  issue(deviceId: string, request: IssueCommandRequest): Observable<IssueCommandResponse> {
    return this.http.post<IssueCommandResponse>(
      `${this.base}/devices/${encodeURIComponent(deviceId)}/commands`,
      request,
      { context: new HttpContext().set(ADMIN_GATED, true) },
    );
  }
}
