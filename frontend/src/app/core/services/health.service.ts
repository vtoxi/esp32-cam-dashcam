import { HttpClient } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable } from 'rxjs';
import { HealthSummary } from '../models';
import { ApiConfigService } from './api-config.service';

@Injectable({ providedIn: 'root' })
export class HealthService {
  private readonly http = inject(HttpClient);
  private readonly config = inject(ApiConfigService);

  get(): Observable<HealthSummary> {
    return this.http.get<HealthSummary>(`${this.config.baseUrl()}/health`);
  }
}
