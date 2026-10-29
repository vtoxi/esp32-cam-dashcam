import { HttpContextToken, HttpInterceptorFn } from '@angular/common/http';
import { inject } from '@angular/core';
import { AdminAuthService } from '../services/admin-auth.service';

/** Marks a request as needing the X-Admin-Key header (see admin-key.interceptor.ts). */
export const ADMIN_GATED = new HttpContextToken<boolean>(() => false);

/**
 * Only routes the frontend itself marks as admin-gated (via the ADMIN_GATED
 * request context token) get the header, so a stale/empty key never rides
 * along on ordinary reads.
 */
export const adminKeyInterceptor: HttpInterceptorFn = (req, next) => {
  const admin = inject(AdminAuthService);
  const key = admin.currentKey();
  if (!key || !req.context.get(ADMIN_GATED)) {
    return next(req);
  }
  return next(req.clone({ setHeaders: { 'X-Admin-Key': key } }));
};
